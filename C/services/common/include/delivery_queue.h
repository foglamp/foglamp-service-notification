#ifndef _DELIVERY_QUEUE_H
#define _DELIVERY_QUEUE_H
/*
 * FogLAMP delivery queue manager.
 *
 * Copyright (c) 2020 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <logger.h>
#include <queue>
#include <condition_variable>
#include <rule_plugin.h>
#include <delivery_plugin.h>
#include <reading_set.h>
#include <notification_subscription.h>
#include <mutex>

/**
 * Class that represents the delivery data to send
 */
class DeliveryDataElement
{
	public:
		DeliveryDataElement(const std::string& deliveryName,
				    const std::string& notificationName,
				    const std::string& triggerReason,
				    const std::string& message,
				    NotificationInstance* instance);

		~DeliveryDataElement();
		const std::string&	getDeliveryName() { return m_deliveryName; };
		const std::string&	getNotificationName() { return m_notificationName; };
		const std::string&	getReason() { return m_reason; };
		const std::string&	getMessage() { return m_message; };
		const NotificationInstance*
					getInstance() { return m_instance; };
		NotificationInstance*
					m_instance;

	private:
		std::string	m_deliveryName;
		std::string	m_notificationName;
		std::string	m_reason;
		std::string	m_message;
};

/**
 * Class that represents the item stored in the queue.
 */
class DeliveryQueueElement
{
	public:
		DeliveryQueueElement(DeliveryDataElement* data);

		~DeliveryQueueElement();

		const std::string&	getName() { return m_name; };
		DeliveryDataElement*	getData() { return m_data; };

		DeliveryPlugin*		getPlugin() { return m_plugin; };
		void			setPlugin(DeliveryPlugin* plugin)
		{
			m_plugin = plugin;
		};

	public:
		unsigned long		m_time;

	private:
		std::string		m_name;
		DeliveryDataElement*	m_data;
		DeliveryPlugin*		m_plugin;
};

/**
 * The DeliveryQueue class.
 *
 * This class handles the delivery items to send,
 * storing data into a std::deque and the processing them.
 */
class DeliveryQueue
{
	public:
		DeliveryQueue(const std::string& serviceName,
			      unsigned long workers);
		~DeliveryQueue();

		static DeliveryQueue*
					getInstance() { return m_instance; };
		const std::string&	getName() const { return m_name; };
		bool			addElement(DeliveryQueueElement* element);
		void			process(int name);
		bool			isRunning() const { return m_running; };
		void			stop();
		std::vector<std::string>
					getIdleQueues()
		{
			std::lock_guard<std::mutex> lockIdle(m_queueMutex);
			std::vector<std::string> ret;
			for (auto q = m_deliveryBusy.begin();
			     q != m_deliveryBusy.end();
			     ++q)
			{
				if (!q->second)
				{
					ret.push_back(q->first);
				}
			}
			return ret;
		};

		void			deleteDeliveryObjects(const std::string& deliveryName)
		{
			m_logger->debug("+++++ Need to remove %s from m_deliveryBusy", deliveryName.c_str());
		}

	private:
		void			processDelivery(DeliveryQueueElement* data);

	private:

		const std::string	m_name;
		static DeliveryQueue*	m_instance;
		bool			m_running;

		// Delivery workers
		std::vector<std::thread*>
					m_queue_workers;

		// Thread handling
		std::condition_variable	m_processCv;
		std::mutex		m_queueWaitMutex;

		// Delivery queues map
		std::map<std::string, std::queue<DeliveryQueueElement *>>
					m_deliveryQueues;
		// Delivery queue mutexes
		std::map<std::string, std::mutex>
				 	m_deliveryMutexes;

		// Delivery concurrency lock
		std::mutex		m_queueMutex;
		// Delivery concurrency map
		std::map<std::string, bool>
					m_deliveryBusy;
		// Logger
		Logger*                 m_logger;
};
#endif
