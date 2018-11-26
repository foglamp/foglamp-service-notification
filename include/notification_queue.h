#ifndef _NOTIFICATION_QUEUE_H
#define _NOTIFICATION_QUEUE_H
/*
 * FogLAMP notification queue manager.
 *
 * Copyright (c) 2018 Dianomic Systems
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

class NotificationQueueElement
{
	public:
		NotificationQueueElement(const string& assetName,
					 ReadingSet* data);

		~NotificationQueueElement();

		const std::string&	getAssetName() { return m_assetName; };
		ReadingSet*		getAssetData() { return m_readings; };

	private:
		std::string		m_assetName;
		ReadingSet*		m_readings;
};

/**
 * The NotificationQueue class.
 */
class NotificationQueue
{
	public:
		NotificationQueue(const std::string& serviceName);
		~NotificationQueue();

		static NotificationQueue*
					getInstance() { return m_instance; };
		const std::string&	getName() const { return m_name; };
		bool			addElement(NotificationQueueElement* element);
		void			process();
		bool			isRunning() const { return m_running; };
		void			stop() { m_running = false; };
		void			processDataSet(NotificationQueueElement* data);

	private:
		const std::string	m_name;
		static NotificationQueue*
					m_instance;
		bool			m_running;
		thread*			m_queue_thread;
		std::mutex		m_qMutex;
		std::condition_variable	m_processCv;
		queue<NotificationQueueElement *>
                                        m_queue;
};

#endif
