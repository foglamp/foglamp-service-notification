/*
 * FogLAMP delivery queue
 *
 * Copyright (c) 2020 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <management_api.h>
#include <management_client.h>
#include <service_record.h>
#include <plugin_manager.h>
#include <plugin_api.h>
#include <plugin.h>
#include <logger.h>
#include <iostream>
#include <string>
#include <datapoint.h>
#include <notification_service.h>
#include <notification_manager.h>
#include <notification_api.h>
#include <notification_subscription.h>
#include <notification_queue.h>
#include <delivery_queue.h>

using namespace std;

DeliveryQueue* DeliveryQueue::m_instance = 0;

/**
 * Process queue worker thread entry point
 *
 * @param    queue	Pointer to DeliveryQueue instance
 * @param    num	The thread number
 */
static void worker(DeliveryQueue* queue, int num)
{
	queue->process(num);
}

/**
 * DeliveryDataElement construcrtor
 *
 * @param    delieveryName	The deliveryName to process
 */
DeliveryDataElement::DeliveryDataElement(const string& deliveryName,
					 const string& notificationName,
					 const string& triggerReason,
					 const string& message,
					 NotificationInstance* instance) :
				m_deliveryName(deliveryName),
                                m_notificationName(notificationName),
                                m_reason(triggerReason),
				m_message(message),
				m_instance(instance)
{
}

/**
 * DeliveryDataElement destructor
 */
DeliveryDataElement::~DeliveryDataElement()
{
}

/**
 * DeliveryQueueElement constructor
 *
 * @param    assetName	The assetName which gets ntotification data
 * @param    data	The delivery data pointer
 */
DeliveryQueueElement::DeliveryQueueElement(DeliveryDataElement* data)
{
	m_name = data->getDeliveryName();
	m_data = data;
	m_time = time(NULL);

	// Get instances
	NotificationManager* instances = NotificationManager::getInstance();

	// Find instance for this rule
	NotificationInstance* instance =
		instances->getNotificationInstance(data->getNotificationName());

	// Save DeliveryPlugin object
	if (instance)
	{
		m_plugin = instance->getDeliveryPlugin();
	}
	else
	{
		m_plugin = NULL;
	}

	// Check whether the DeliveryDataElement is signalling the end of received deliveries
	if (data->m_instance == NULL)
	{
		if (m_plugin)
		{
			Logger::getLogger()->debug("Calling plugin_shutdown for delivery %s",
						   data->getDeliveryName().c_str());

			// Call plugin_shutdown
			m_plugin->shutdown();

			// Remove the object
			delete m_plugin;
	
			// Set NULL
			m_plugin = NULL;
		}
	}
}

/**
 * DeliveryQueueElement destructor
 */
DeliveryQueueElement::~DeliveryQueueElement()
{
	delete m_data;
}

/**
 * Constructor for the DeliveryQueue class
 *
 * @param    notificationName	NotificationService name
 * @param    numWorkers		The maximum number of worker threads
 */
DeliveryQueue::DeliveryQueue(const string& notificationName,
			     unsigned long numWorkers) :
				     m_name(notificationName)
{
	// Set running
	m_running = true;
	// Set instance
	m_instance = this;

	// Get logger
	m_logger = Logger::getLogger();

	// Start process queue threads
	for (int i = 0; i < numWorkers; ++i)
	{
		m_queue_workers.push_back(new thread(worker, this, i));
#ifdef DEBUG_DELIVERY_QUEUE
		m_logger->debug("Started delivery queue thread [%d]", i);
#endif
	}

	m_logger->info("Notification delivery queue has %lu worker threads.",
			m_queue_workers.size());
}

/**
 * DeliveryQueue destructor
 */
DeliveryQueue::~DeliveryQueue()
{
	for (int i = 0; i < m_queue_workers.size(); ++i)
	{
		delete m_queue_workers[i];
	}
}

/**
 * Stop processing delivery objects
 */
void DeliveryQueue::stop()
{

	m_running = false;

	m_logger->debug("DeliveryQueue has received stop request: there are %d active queues",
			m_deliveryQueues.size());

	for (auto it = m_deliveryQueues.begin();
	     it != m_deliveryQueues.end();
	     ++it)
	{
		m_logger->debug("DeliveryQueue [%s] has received stop request: "
				"there are still %d elements to process",
				(*it).first.c_str(),
				(*it).second.size());
	}

	// Unlock all waiting threads
	m_processCv.notify_all();

	// Waiting for the delivery threads to complete
	for (int i = 0; i < m_queue_workers.size(); ++i)
	{
		if (m_queue_workers[i]->joinable())
		{
			m_queue_workers[i]->join();
#ifdef DEBUG_DELIVERY_QUEUE
			m_logger->debug("Joined delivery thread [%d]", i);
#endif
		}
	}

	return;
}

/**
 * Add an element to the queue
 *
 * @param    element		The element to add the queue.
 * @return			True on succes, false otherwise.
 */
bool DeliveryQueue::addElement(DeliveryQueueElement* element)
{
	if (!m_running)
	{
		// Don't add new elements if queue is being stopped
		delete element;
		return true;
	}

	// Create entry in the delivery concurrency map
	{
		lock_guard<std::mutex> lockConcurrency(m_queueMutex);
		// Fist time element->getName() is seen, then busy flag is false
		if (m_deliveryBusy.find(element->getName()) == m_deliveryBusy.end())
		{
			m_deliveryBusy[element->getName()] = false;
		}
	}

	// Insert element into the named queue
	{
		// Lock per delivery name
		lock_guard<mutex> deliveryLoadLock(m_deliveryMutexes[element->getName()]);

		// Add element to the queue (this create entry in the queue map if not existent)
		m_deliveryQueues[element->getName()].push(element);

#ifdef DEBUG_DELIVERY_QUEUE
		m_logger->debug("DeliveryQueue [%s] currently has %lu elements",
				element->getName().c_str(),
				m_deliveryQueues[element->getName()].size());
#endif
	}

	// Notify all waiting threads
	m_processCv.notify_all();

#ifdef DEBUG_DELIVERY_QUEUE
	m_logger->debug("Added delivery element for deliveryName %s "
			"to the delivery queue, time element is added %lu",
			element->getName().c_str(),
			element->m_time);
#endif

	return true;
}

/**
 * Process data in the queue
 *
 * @param num	The thread number
 */
void DeliveryQueue::process(int num)
{
	m_logger->debug("DeliveryQueue thread [%d] started", num);

	bool doProcess = true;

	while (doProcess)
	{
		bool ret;
		DeliveryQueueElement* data = NULL;

		// Get vector of idle queues
		vector<string> listIdles = this->getIdleQueues();
		int idleCount = listIdles.size();

		// Iterate through the idle queues
		for (auto idl = listIdles.begin();
		     idl != listIdles.end();
		     ++idl)
		{
			{
				// Lock named delivery queue
				unique_lock<mutex> deliveryLock(m_deliveryMutexes[*idl]);
				bool nextQueue = false;

				// Check first the queue is empty
				if (m_deliveryQueues[*idl].empty())
				{
					// Descrease idle counter
					idleCount--;

					// No more idle queues with data to process
					if (!idleCount)
					{
						// process be stopped?
						doProcess = m_running;

						// Release lock
						deliveryLock.unlock();
						break;
					}

					// This queue has no data: go to the next idle queue
					// Release lock
					deliveryLock.unlock();
					continue;
				}

				// Get first element in the queue
				data = m_deliveryQueues[*idl].front();

				// Remove the item
				m_deliveryQueues[*idl].pop();
			}

			// Process delivery data
			if (data)
			{
#ifdef DEBUG_DELIVERY_QUEUE
				m_logger->debug("DeliveryQueue Thread [%d] READY to process "
						"delivery element for delivery %s, "
						"time element was added %lu, plugin instance %p",
						num,
						data->getName().c_str(),
						data->m_time,
						data->getPlugin());
#endif
				{
					lock_guard<std::mutex> lockQueue(m_queueMutex);
					// Set busy indicator
					m_deliveryBusy[*idl] = true;
				}

				// Process delivery data, no lock is held during this call
				this->processDelivery(data);

#ifdef DEBUG_DELIVERY_QUEUE
				m_logger->debug("DeliveryQueue Thread [%d] done processing "
						"delivery element for delivery name %s, "
						"time element was added %lu",
						num,
						data->getName().c_str(),
						data->m_time);
#endif

				// Set deliveryName concurrency to false in the map, lock is required
				lock_guard<std::mutex> lockQueue(m_queueMutex);
				m_deliveryBusy[*idl] = false;
			}

			// Remove data object
			delete data;
		}

		if (doProcess &&
		    !idleCount)
		{
			unique_lock<std::mutex> lockForData(m_queueWaitMutex);
#ifdef DEBUG_DELIVERY_QUEUE
			m_logger->debug("Delivery thread [%d] ALL delivery queues are empty",
					num);
#endif
			// No data, wait util notified
			m_processCv.wait(lockForData);
		}
	}

#ifdef DEBUG_DELIVERY_QUEUE
	m_logger->debug("DeliveryQueue thread [%d], delivery queuing stopped: "
			"there are %d queues active",
			num,
			m_deliveryQueues.size());
#endif
	for (auto it = m_deliveryQueues.begin();
	     it != m_deliveryQueues.end();
	     ++it)
	{
		m_logger->info("DeliveryQueue thread [%d], queue [%s] is being stopped: "
				"there are still %d elements to process",
				num,
				(*it).first.c_str(),
				(*it).second.size());
	}

#ifdef DEBUG_DELIVERY_QUEUE
	m_logger->debug("DeliveryQueue thread [%d] exits", num);
#endif
	// Notify any waiting thread
	m_processCv.notify_all();

	return;
}

/**
 * Process a delivery data element
 *
 * Call plugin_deliver
 *
 * If the received delivery data element has data->getPlugin() is NULL
 * then we know then the plugin has been removed at notification instance shutdown
 *
 * After this particular delivery data element there will be no more entries in the delivery queue.
 *
 * @param   elem	Element in the queue
 */
void DeliveryQueue::processDelivery(DeliveryQueueElement* elem)
{

	if (elem->getName().compare("deliveryB") == 0)
	{
		std::this_thread::sleep_for (std::chrono::milliseconds(5200));
	}

	// Get instances
	NotificationManager* instances = NotificationManager::getInstance();

	// Find instance for this rule
	NotificationInstance* instance =
	instances->getNotificationInstance(elem->getData()->getNotificationName());

	// Check deliveryPlugin object
	if (instance && elem->getPlugin())
	{
		// Check whether current instance has a different plugin from saved one
		if (instance->getDeliveryPlugin() != elem->getPlugin())
		{
			// Set new plugin
			elem->setPlugin(instance->getDeliveryPlugin());
		}
	}

	// Check whether delivery plugin is still available
	if (elem->getPlugin())
	{
		// Call plugin_deliver
		elem->getPlugin()->deliver(elem->getName(),
					   elem->getData()->getNotificationName(),
					   elem->getData()->getReason(),
					   elem->getData()->getMessage());
	}
#ifdef DEBUG_DELIVERY_QUEUE
	else
	{
		m_logger->debug("Delivery plugin was removed: plugin_deliver "
				"not being called for %s, time element was added %lu",
				elem->getName().c_str(),
				elem->m_time);
	}
#endif
}
