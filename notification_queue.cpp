/*
 * FogLAMP notification queue
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <notification_service.h>
#include <management_api.h>
#include <management_client.h>
#include <service_record.h>
#include <plugin_manager.h>
#include <plugin_api.h>
#include <plugin.h>
#include <logger.h>
#include <iostream>
#include <string>
#include <notification_queue.h>

using namespace std;

NotificationQueue* NotificationQueue::m_instance = 0;

/**
 * Process queue worker thread entry point
 *
 * @param    queue	Pointer to NotificationQueue instance
 */
static void worker(NotificationQueue* queue)
{
	queue->process();
}

/**
 * Constructor for the NotificationQueue class
 *
 * @param    notificationName	NotificationService name
 */
NotificationQueue::NotificationQueue(const string& notificationName) :
				     m_name(notificationName)
{
	// Set running
	m_running = true;
	// Set instance
	m_instance = this;
	// Start process queue thread
	m_queue_thread = new thread(worker, this);
}

/**
 * Destructor
 */
NotificationQueue::~NotificationQueue()
{
	delete m_queue_thread;
}

/**
 * NotificatioQueueElement constructor
 *
 * Put ReadingSet data for assetNmae into the queue
 *
 * @param    assetName	The assetName which gets ntotification data
 * @param    data	The readings data pointer
 */
NotificationQueueElement::NotificationQueueElement(const string& assetName,
						   ReadingSet* data) :
						   m_assetName(assetName),
						   m_readings(data)
{
	Logger::getLogger()->debug("NotificationQueueElement [" + \
				   assetName + "] # readings = " + \
				   (data ? to_string(data->getCount()) : string("NO_DATA"))); 
}

/**
 * NotificatioQueueElement destructor
 */
NotificationQueueElement::~NotificationQueueElement()
{
	// Remove readings
	delete m_readings;
}

/**
 * Add an element to the queue
 *
 * @param    element		The elemnennt to add the queue.
 * @return			True on succes, false otherwise.
 */
bool NotificationQueue::addElement(NotificationQueueElement* element)
{
	lock_guard<mutex> loadLock(m_qMutex);

	m_queue.push(element);

	Logger::getLogger()->debug("Element added to queue, asset [" + element->getAssetName() + \
				  "], #readings " + to_string(element->getAssetData()->getCount()));

	m_processCv.notify_all();

	return true;
}

/**
 * Process data in the queue
 */
void NotificationQueue::process()
{
	while (m_running)
	{
		NotificationQueueElement* data = NULL;
		// Get data from the queue
		{
			unique_lock<mutex> sendLock(m_qMutex);
			while(m_queue.empty())
			{       
				// No data, wait util notified
				m_processCv.wait(sendLock);
			}
			// Get first element in the queue
			data = m_queue.front();
			// Remove the item
			m_queue.pop();
		}

		if (data)
		{       
			// Process data
			this->processDataSet(data);
			delete data;
		}
	}
}

/**
 * Process data into the queue
 *
 * @param    data	Current item int the queue
 */
void NotificationQueue::processDataSet(NotificationQueueElement* data)
{
	// Process data ...
}
