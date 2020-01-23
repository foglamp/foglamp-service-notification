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
 */
static void worker(DeliveryQueue* queue, int name)
{
	queue->process(name);
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
		Logger::getLogger()->debug(">>>  Need to shutdown deliveryPlugin for delivery %s",
					   data->getDeliveryName().c_str());
		if (m_plugin)
		{
			Logger::getLogger()->debug(">>> Calling plugin SHUTDOWN for delivery %s",
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
 */
DeliveryQueue::DeliveryQueue(const string& notificationName) :
				     m_name(notificationName)
{
	// Set running
	m_running = true;
	// Set instance
	m_instance = this;

	// Start process queue threads TODO, use a configurable pool
	m_queue_thread = new thread(worker, this, 1);
	m_queue_thread2 = new thread(worker, this, 2);
	m_queue_thread3 = new thread(worker, this, 3);

	// Get logger
	m_logger = Logger::getLogger();
}

/**
 * DeliveryQueue destructor
 */
DeliveryQueue::~DeliveryQueue()
{
	delete m_queue_thread;
	delete m_queue_thread2;
	delete m_queue_thread3;
}

/**
 * Stop processing delivery objects
 */
void DeliveryQueue::stop()
{

	m_running = false;

	m_logger->debug("DeliveryQueue has received stop request: "
			"there are currently %ld elments to process",
			m_queue.size());

	m_processCv.notify_all();

	// Waiting for the process thread to complete: TODO use a pool
	m_queue_thread->join();
	m_queue_thread2->join();
	m_queue_thread3->join();
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

	lock_guard<mutex> loadLock(m_queueMutex);

	// Add elemet to the queue
	m_queue.push_back(element);

	m_processCv.notify_all();

	m_logger->debug(">>> Added dewlivery element for deliveryName %s "
			"to the delivery queue, time element is added %lu",
			element->getName().c_str(),
			element->m_time);

	return true;
}

/**
 * Process data in the queue
 */
void DeliveryQueue::process(int name)
{
	m_logger->debug(">>> DeliveryQueue thread [%d] started", name);

	bool doProcess = true;

	while (doProcess)
	{
		bool ret;
		DeliveryQueueElement* data = NULL;
		// Get data from the queue
		{
			unique_lock<mutex> sendLock(m_queueMutex);
			while (m_queue.empty())
			{
				if (!m_running)
				{
					// No data and load thread is not running.
					doProcess = false;
					break;
				}
				else
				{
					// No data, wait util notified
					m_processCv.wait(sendLock);
				}
			}

			if (doProcess)
			{
				// Get first element in the queue
				data = m_queue.front();

				// Remove the item
				m_queue.pop_front();
			}
		}

		// Now handling data pulled from the queue
		if (data)
		{
			string deliveryName = data->getName();
			unique_lock<mutex> sendLock(m_queueMutex);

			auto it = m_concurrentDelivery.find(deliveryName);
			if (it == m_concurrentDelivery.end())
			{
				// Set m_concurrentDelivery for same deliveryName concurrency
				m_concurrentDelivery[deliveryName] = true;

				// Release lock
				sendLock.unlock();

				m_logger->debug(">>> DeliveryQueue Thread [%d] handling delivery element %s, "
						"time element was added %lu",
						name,
						data->getName().c_str(),
						data->m_time);
			}
			else
			{
				// Small sleep before adding data back into the queue
				std::this_thread::sleep_for (std::chrono::milliseconds(10));

				// Put it at the beginning of the queue
				m_queue.push_front(data);

				// Release lock
				sendLock.unlock();

				// Process queue again
				continue;
			}

			m_logger->debug(">>> DeliveryQueue Thread [%d] ready to process delivery element%s, "
					"time element was added %lu",
					name,
					data->getName().c_str(),
					data->m_time);

			// Process delivery data
			this->processDelivery(data);

			m_logger->debug(">>> DeliveryQueue Thread %d] DONE processing delivery element %s, "
					"time element was added %lu",
					name,
					data->getName().c_str(),
					data->m_time);

			// Remove deliveryName form concurrency map, lock is required
			sendLock.lock();
			m_concurrentDelivery.erase(deliveryName);
			sendLock.unlock();

			// Remove data object
			delete data;
		}
	}

	m_logger->debug("DeliveryQueue stopped: there are still %ld elements to process",
			m_queue.size());
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
 * @param   data	Data element in the queue
 */
void DeliveryQueue::processDelivery(DeliveryQueueElement* data)
{
	if (data->getPlugin())
	{
		data->getPlugin()->deliver(data->getName(),
					data->getData()->getNotificationName(),
					data->getData()->getReason(),
					data->getData()->getMessage());

		m_logger->debug(">>> CALLED plugin_deliver for %s, time element was added %lu",
				data->getName().c_str(),
				data->m_time);
	}
	else
	{
		m_logger->debug(">>> PLUGIN was removed: plugin_deliver not called "
				"for %s, time element was added %lu",
				data->getName().c_str(),
				data->m_time);
	}
}
