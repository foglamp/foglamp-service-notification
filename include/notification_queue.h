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
#include <notification_subscription.h>

/**
 * Class that represents the notification data stored in the per rule buffers.
 */
class NotificationDataElement
{
	public:
		NotificationDataElement(const std::string& asset,
					const std::string& rule,
					ReadingSet* data);
		~NotificationDataElement();
		const std::string&	getAssetName() { return m_asset; };
		const std::string&	getRuleName() { return m_ruleName; };
		ReadingSet*		getData() { return m_data; };

	private:
		const std::string	m_asset;
		const std::string	m_ruleName;
		ReadingSet*		m_data;
};

/**
 * Class that represents the item stored in the queue.
 */
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
 *
 * This class handles the notification items received,
 * storing data into a std::queue and the processing them.
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
		void			stop();

	private:
		void			processDataSet(NotificationQueueElement* data);
		void			feedAllDataBuffers(NotificationQueueElement* data);
		void			processAllDataBuffers(const std::string& assetName);
		bool			feedDataBuffer(const std::string& ruleName,
						       const std::string& assetName,
						       ReadingSet* assetData);
		bool			processDataBuffer(map<std::string, std::string>&,
							  const string&ruleName,
							  const string& assetName,
							  NotificationDetail& element);
		vector<NotificationDataElement*>&
					getBufferData(const std::string& ruleName,
						      const std::string& assetName);
		void			clearBufferData(const std::string& ruleName,
							const std::string& assetName);
		void 			keepBufferData(const std::string& ruleName,
						       const std::string& assetName,
						       unsigned long num);
		bool			processAllReadings(NotificationDetail& info,
							   vector<NotificationDataElement *>& readingsData,
							   map<std::string, std::string>& results);
		bool			evalRule(map<std::string, std::string>& results,
						 NotificationRule* rule);
		string			processLastBuffer(NotificationDataElement* data);
		bool			sendNotification(map<string,string>& results,
							 SubscriptionElement& subscription);
		map<string, string>	processAllBuffers(vector<NotificationDataElement *>& readingsData,
							  EvaluationType::EVAL_TYPE type,
							  unsigned long timeInterval);
		void			setValue(map<string, Datapoint *>& result,
						 Datapoint* d,
						 EvaluationType::EVAL_TYPE type);
		void			setMinValue(map<string, Datapoint *>& result,
						    const string& key,
						    DatapointValue& val);
		void			setMaxValue(map<string, Datapoint *>& result,
						    const string& key,
						    DatapointValue& val);
		void			setSumValues(map<string, Datapoint *>& result,
						    const string& key,
						    DatapointValue& val);

	private:
		/**
		 * This class represents the per rule data container.
		 * Notification data stored ias vector, per asset name.
		 */
		class NotificationDataBuffer
		{
			public:
				NotificationDataBuffer() {};
				~NotificationDataBuffer() {};

				// Append data into m_assetData[assetName]
				void append(const std::string& assetName,
					NotificationDataElement* data)
				{
					m_assetData[assetName].push_back(data);
				};
				// Return m_assetData[assetName] data
				vector<NotificationDataElement*>&
					getData(const std::string& assetName)
				{
					return m_assetData[assetName];
				};

			private:
				map<std::string, vector<NotificationDataElement*>>
					m_assetData;
		};

		const std::string	m_name;
		static NotificationQueue*
					m_instance;
		bool			m_running;
		thread*			m_queue_thread;
		std::mutex		m_qMutex;
		std::condition_variable	m_processCv;
		// Queue for received notifications
		queue<NotificationQueueElement *>
                                        m_queue;
		// Per rule priocess buffers
		map<std::string, NotificationDataBuffer>
					m_ruleBuffers;
		Logger*                 m_logger;
};
#endif
