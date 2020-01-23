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
#include <deque>
#include <condition_variable>
#include <rule_plugin.h>
#include <delivery_plugin.h>
#include <reading_set.h>
#include <notification_subscription.h>

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
		DeliveryQueue(const std::string& serviceName);
		~DeliveryQueue();

		static DeliveryQueue*
					getInstance() { return m_instance; };
		const std::string&	getName() const { return m_name; };
		bool			addElement(DeliveryQueueElement* element);
		void			process(int name);
		bool			isRunning() const { return m_running; };
		void			stop();

	private:
		void			processDelivery(DeliveryQueueElement* data);

	private:

		const std::string	m_name;
		static DeliveryQueue*	m_instance;
		bool			m_running;

		// Conusmer threads: TODO add a pool
		std::thread*		m_queue_thread;
		std::thread*		m_queue_thread2;
		std::thread*		m_queue_thread3;
		std::mutex		m_queueMutex;
		std::condition_variable	m_processCv;

		// Double-ended queue for deliveries
		std::deque<DeliveryQueueElement *>
                                        m_queue;

		// Map for same delivery name handling
		std::map<std::string, bool>
					m_concurrentDelivery;

		// Logger
		Logger*                 m_logger;
};
#endif
