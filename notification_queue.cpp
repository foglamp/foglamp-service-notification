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
#include <datapoint.h>

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
 * NotificationDataElement construcrtor
 *
 * @param    ruleName		The ruleName which asseName belongs to
 * @param    assetName		The asseName for current data
 * @param    assetData		The ReadingSet data related to assetName
 */
NotificationDataElement::NotificationDataElement(const string& ruleName,
						 const string& assetName,
						 ReadingSet* assetData) :
						 m_ruleName(ruleName),
						 m_asset(assetName),
						 m_data(assetData)
{
#ifdef QUEUE_DEBUG_DATA
	const vector<Reading *>& readings = assetData->getAllReadings();
	for (auto m = readings.begin();
		  m != readings.end();
		  ++m)
	{
		assert((*m)->getAssetName().compare(assetName) == 0);
	}
#endif
}

/**
 * NotificationDataElement destructor
 */
NotificationDataElement::~NotificationDataElement()
{
	m_data->removeAll();
	delete m_data;
}

/**
 * NotificatioQueueElement constructor
 *
 * @param    assetName	The assetName which gets ntotification data
 * @param    data	The readings data pointer
 */
NotificationQueueElement::NotificationQueueElement(const string& assetName,
						   ReadingSet* data) :
						   m_assetName(assetName),
						   m_readings(data)
{
#ifdef QUEUE_DEBUG_DATA
	Logger::getLogger()->debug("addind to queue a NotificationQueueElement [" + \
				   assetName + "] of # readings = " + \
				   (data ? to_string(data->getCount()) : string("NO_DATA")));
	// Debug check
	const vector<Reading *>& readings = data->getAllReadings();
        for (auto m = readings.begin();
             m != readings.end();
             ++m)
        {
                assert((*m)->getAssetName().compare(assetName) == 0);
        }
#endif
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
 * NotificatioQueue destructor
 */
NotificationQueue::~NotificationQueue()
{
	delete m_queue_thread;
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

#ifdef QUEUE_DEBUG_DATA
	Logger::getLogger()->debug("Element added to queue, asset [" + element->getAssetName() + \
				  "], #readings " + to_string(element->getAssetData()->getCount()));
#endif

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

#ifdef QUEUE_DEBUG_DATA
		Logger::getLogger()->debug("Queue processing done: "
					   "queue has %ld elements",
					   m_queue.size());
#endif
	}

#ifdef QUEUE_DEBUG_DATA
	Logger::getLogger()->debug("Queue stopped: size %ld elments",
				   m_queue.size());
#endif
}

/**
 * Process a queue data element
 *
 * @param   data	Data element in the queue
 */
void NotificationQueue::processDataSet(NotificationQueueElement* data)
{
	/**
	 * Here we have one queue entry, for one assetName only,
	 *
	 * (1) Add data to each data buffer[ruleName] related to this assetName
	 * (2) For each ruleName related to assetName process data in buffer[ruleName]
	 */

	// (1) feed al rule buffers
	this->feedAllDataBuffers(data);
	// (2) process all data in all rule buffers for given assetName
	this->processAllDataBuffers(data->getAssetName());
}

/**
 * Append input data in ALL process data buffers which need assetName
 * assetName has some rules associated: ruleA, ... ruleN
 * Append same data in buffera[ruleA][assetName] ... buffera[ruleN][assetName]
 *
 * @param    data	Current item in the queue
 */
void NotificationQueue::feedAllDataBuffers(NotificationQueueElement* data)
{
	// Get assetName in the data element
	string assetName = data->getAssetName();

	// Get all subscriptions related the asetName
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	std::vector<SubscriptionElement>& subscriptionItems = subscriptions->getSubscription(assetName);

	for (auto it = subscriptionItems.begin();
		  it != subscriptionItems.end();
		  ++it)
	{
		// Get subscription ruleName for the assetName
		string ruleName = (*it).getRule()->getName();

		// Feed buffer[ruleName][theAsset] with Readings data
		this->feedDataBuffer(ruleName, assetName, data->getAssetData());
	}
}

/**
 * Append a ReadingSet copy into the process data buffers[rule][asset]
 *
 * @param    ruleName		The ruleName
 * @param    assetName		The assetName
 * @param    assetData		The ReadingSet data
 * @return			True on success, false otherwise
 */
bool NotificationQueue::feedDataBuffer(const std::string& ruleName,
				       const std::string& assetName,
				       ReadingSet* assetData)
{
	// Create a ReadingSet copy
	ReadingSet* readingsCopy = new ReadingSet;
	readingsCopy->append(assetData);

	NotificationDataElement* newdata = new NotificationDataElement(ruleName,
								       assetName,
								       readingsCopy);
	if (!newdata)
	{
		return false;
	}

	// Append data
	NotificationDataBuffer& dataContainer = this->m_ruleBuffers[ruleName];
	dataContainer.append(assetName, newdata);

	return true;
}

/**
 * Get content of data buffers[rule][asset]
 *
 * @param    ruleName		The ruleName
 * @param    assetName		The assetName
 * @return			Vector of data in the buffer
 */
vector<NotificationDataElement*>& NotificationQueue::getBufferData(const std::string& ruleName,
								   const std::string& assetName)
{
	NotificationDataBuffer& dataContainer = this->m_ruleBuffers[ruleName];
	return dataContainer.getData(assetName);
}

/**
 * Clear all in data buffers[rule][asset]
 *
 * @param    ruleName		The ruleName
 * @param    assetName		The assetName
 */
void NotificationQueue::clearBufferData(const std::string& ruleName,
					const std::string& assetName)
{
	NotificationDataBuffer& dataContainer = this->m_ruleBuffers[ruleName];
	vector<NotificationDataElement*>& data = dataContainer.getData(assetName);
	for (auto it = data.begin();
                  it != data.end();
                  ++it)
        {
		// Free object data
		delete(*it);
	}
	// Remove all vector objects
	data.clear();
}

/**
 * Keep some data in buffers[rule][asset]
 *
 * @param    ruleName		The ruleName
 * @param    assetName		The assetName
 * @param    num		The number of elements
 *				to keep in buffers[rule][asset]
 */
void NotificationQueue::keepBufferData(const std::string& ruleName,
					   const std::string& assetName,
					   unsigned long num)
{
	NotificationDataBuffer& dataContainer = this->m_ruleBuffers[ruleName];
	vector<NotificationDataElement*>& data = dataContainer.getData(assetName);

	// Save current size
	unsigned long initialSize = data.size();
	unsigned long removed = 0;

	for (auto it = data.begin();
		  it != data.end();
		  ++it)
	{
		if (data.size() <= num)
		{
			break;
		}

		removed++;
		// Free object data
		delete(*it);
		//Remove current vector object
		data.erase(it);
	}
#ifdef QUEUE_DEBUG_DATA
	 Logger::getLogger()->debug(Keeping Buffers for " + \
				    assetName + " of " + ruleName + \
				    " removed " + to_string(removed) + "/" + \
				    to_string(initialSize) + " now has size " + \
				    to_string(data.size()));

	assert(num == data.size());
#endif
}

/**
 * Process data in buffers[rule][asset]
 *
 * @param    results		Map with output data, per assetName
 * @param    ruleName		The ruleName
 * @param    assetName		The assetName
 * @param    info		The notification info:
 *				evaluation type and time period
 * @return			True if processed data found or false.
 */
bool NotificationQueue::processDataBuffer(map<string, string>& results,
					  const string& ruleName,
					  const string& assetName,
					  NotificationDetail& info)
{
	bool evalRule = false;

	assert(assetName.compare(info.getAssetName()) == 0);
	assert(ruleName.compare(info.getRuleName()) == 0);

	// Get all data for assetName in the buffer[ruleName]
	vector<NotificationDataElement*>& readingsData = this->getBufferData(ruleName,
									     assetName);

	if (readingsData.size() == 0)
	{
		return false;
	}

#ifdef QUEUE_DEBUG_DATA
	for (auto c = readingsData.begin();
                  c != readingsData.end();
                  ++c)
        {
                vector<Reading *>*s = (*c)->getData()->getAllReadingsPtr();
                for (auto r = s->begin();
                          r != s->end();
                          ++r)
                {
                        assert(assetName.compare((*r)->getAssetName()) == 0);
                }
        }
#endif

	// Process all reading data in the buffer
	return this->processAllReadings(info, readingsData, results);
}

/**
 * Call rule plugin_eval with notification JSON data
 *
 * @param    results	Ready notification results
 * @param    rule	The RulePlugin instance
 * @return		True if the notification has triggered,
 *			false otherwise
 */
bool NotificationQueue::evalRule(map<string, string>& results,
				 NotificationRule* rule)
{
	string evalJSON = "{ ";
	for (auto mm = results.begin();
		  mm != results.end();
		  ++mm)
	{
		evalJSON += "\"" + (*mm).first + "\" : ";
		evalJSON += (*mm).second;
		if (next(mm, 1) != results.end())
		{
			evalJSON += ", " ; 	
		}

		// Clear all data in buffer buffers[rule][asset]
		this->clearBufferData(rule->getName(), (*mm).first);
	}
	evalJSON += " }" ; 	

	// Call plugin_eval
	return rule->getPlugin()->eval(evalJSON);
}

/**
 * Process all data buffers for a given assetName
 *
 * The assetName might belong to differen rules:
 * iterate through all rules and process
 * the rule_buffers[ruleName][assetName]
 *
 * If a notification is ready call rule plugin_eval
 * and delivery plugin_deliver if notification has to be sent
 *
 * @param    assetName		Current assetName
 *				that is receiving notifications data
 */
void NotificationQueue::processAllDataBuffers(const string& assetName)
{
	// Get subscriptions
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	// Get all subscriptions for assetName
	std::vector<SubscriptionElement>& registeredItems = subscriptions->getSubscription(assetName);

	for (auto it = registeredItems.begin();
		  it != registeredItems.end();
		  ++it)
	{
		bool evalRule = false;

		// Get ruleName
		string ruleName = (*it).getRule()->getName();
		// Get all assests belonging to current rule
		vector<NotificationDetail>& assets = (*it).getRule()->getAssets();

		// Results, for all assetNames belonging to current rule
		map<string, string> results;

		for (auto itr = assets.begin();
			  itr != assets.end();
			  ++itr)
		{
			// Process the buffer[rule][asset]
			evalRule = this->processDataBuffer(results,
							   ruleName,
							   (*itr).getAssetName(),
							   *itr);
		}

		// Check results size first
		if (results.size() < assets.size())
		{
			// Notification is not ready yet
			evalRule = false;
		}

		if (evalRule)
		{
			// Call rule "plugin_eval"
			bool ret = this->evalRule(results, (*it).getRule());
			// Clear results data
			results.clear();

			if (ret)
			{
				// Get notification instance
				NotificationInstance* instance = (*it).getInstance();
				// Call rule "plugin_reason"
				string reason = (*it).getRule()->getPlugin()->reason();

				switch (instance->getType())
				{
				case NotificationInstance::Retriggered:
				{
					DeliveryPlugin* plugin = instance->getDeliveryPlugin();
					string customText = instance->getDelivery()->getText();

					bool retCode = plugin->deliver(instance->getDelivery()->getName(),
								       instance->getDelivery()->getNotificationName(),
								       reason,
								       (customText.empty() ?
									"ALERT for " + ruleName :
									instance->getDelivery()->getText()));
					break;
				}
				case NotificationInstance::OneShot:
				// Not ready yet
				case NotificationInstance::Toggled:
				// Not ready yet
				default:
					break;
				}
			}
		}
	}
}

/**
 * Process add Readings data in the data buffers
 *
 * @param    info		The notification details
 * @param    readingsData	Data vector in the buffer
 * @param    results		Results map to update
 * @return			True if the notification is ready,
 *				false otherwise
 */
bool NotificationQueue::processAllReadings(NotificationDetail& info,
					   vector<NotificationDataElement *>& readingsData,
					   map<string, string>& results)
{
	bool evalRule = false;
	string assetName = info.getAssetName();
	string ruleName = info.getRuleName();

#ifdef QUEUE_DEBUG_DATA
	// check
	for (auto c = readingsData.begin();
		  c != readingsData.end();
		  ++c)
	{
		vector<Reading *>*s = (*c)->getData()->getAllReadingsPtr();
		for (auto r = s->begin();
			  r != s->end();
			  ++r)
		{
			assert(assetName.compare((*r)->getAssetName()) == 0);
		}
	}
#endif

	switch(info.getType())
	{
	case EvaluationType::Latest:
		// * Process only last buffer
		{
		string output = this->processLastBuffer(readingsData.back());
		if (!output.empty())
		{
			// This notification is ready
			evalRule = true;
			// Update data in the output buffer
			results[assetName] = string(output);
		}
		break;
		}

	case EvaluationType::Minimum:
		// Not ready yet
	case EvaluationType::Maximum:
		// Not ready yet
	case EvaluationType::Average:
		// Not ready yet
	case EvaluationType::Window:
	default:
		// Not ready yet
		break;
	}

	return evalRule;
}

/**
 * Process the last data buffer and return
 * a JSON string with all datapoints of the last
 * Reading in the ReadingSet
 *
 * @param    data	The last buffer data content
 * @return		JSON string with readings data
 */
string NotificationQueue::processLastBuffer(NotificationDataElement* data)
{
	string output;
	if (!data || !data->getData())
	{
		return output;
	}

	// Get vector of Reading from ReadingSet
	ReadingSet* dataSet = data->getData();
        const std::vector<Reading *>& readings = dataSet->getAllReadings();

	if (!readings.size())
	{
		return output;
	}
	else
	{
		string ret ="{ ";

		// Get the last Reading in the set
		std::vector<Datapoint *>& dataPoints = readings.back()->getReadingData();
		for (auto d = dataPoints.begin();
			  d != dataPoints.end();
			  ++d)
		{
			if (d != dataPoints.begin())
			{
				ret += ", ";
			}
			ret += (*d)->toJSONProperty();
		}
		ret += " }";

		// Just keep one buffer
		this->keepBufferData(data->getRuleName(),
					data->getAssetName(),
					1);

		return ret;
	}
}
