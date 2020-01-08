/*
 * FogLAMP notification queue
 *
 * Copyright (c) 2018 Dianomic Systems
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

static void addReadyData(const map<string, string>& readyData,
			     string& output);
static void deliverData(NotificationRule* rule,
			const std::multimap<uint64_t, Reading*>& itemData,
			const map<string, string>& readyData);
static void deliverNotification(NotificationRule* rule,
				const std::string& data);

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
	// Set element creation time
	m_time = time(NULL);

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
	m_logger->debug("addind to queue a NotificationQueueElement [" + \
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
	time(&m_qTime);
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

	// Get logger
	m_logger = Logger::getLogger();
}

/**
 * NotificatioQueue destructor
 */
NotificationQueue::~NotificationQueue()
{
	delete m_queue_thread;
}

/**
 * Process data still in the buffers
 */
void NotificationQueue::stop()
{

	m_running = false;

	m_processCv.notify_all();

	// Waiting for the process thread to complete
	m_queue_thread->join();

	// NotifictionQueue is empty now: clear all remaining data

	// Get the subscriptions instance
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	if (!subscriptions)
	{
		return;
	}

	NotificationManager* manager = NotificationManager::getInstance();

	// NOTE:
	//
	// Notificatiion API server is down: we cannot receive any configuration change
	// so we don't need to lock subscriptions object
	//
        // Get all subscriptions for assetName
	std::map<std::string, std::vector<SubscriptionElement>>&
		registeredItems = subscriptions->getAllSubscriptions();

	lock_guard<mutex> guard(manager->m_instancesMutex);
	// Iterate trough subscriptions
	for (auto it = registeredItems.begin();
		  it != registeredItems.end();
		  ++it)
	{
		for (auto s = (*it).second.begin();
			  s != (*it).second.end();
			  ++s)
		{
			// Get notification rule object
			string notificationName = (*s).getNotificationName();
			NotificationInstance* instance = manager->getNotificationInstance(notificationName);

			// Get ruleName
			if (instance &&
			    instance->getRule())
			{
				string ruleName = instance->getRule()->getName();
				// Get all assests belonging to current rule
				vector<NotificationDetail>& assets = instance->getRule()->getAssets();

				//Iterate trough assets
				for (auto itr = assets.begin();
					  itr != assets.end();
					   ++itr)
				{
					// Remove all buffers:
					// queue process is donwn, queue lock not needed
					this->clearBufferData(ruleName, (*itr).getAssetName());
				}
			}
		}
	}
}

/**
 * Add an element to the queue
 *
 * @param    element		The element to add the queue.
 * @return			True on succes, false otherwise.
 */
bool NotificationQueue::addElement(NotificationQueueElement* element)
{
	if (!m_running)
	{
		// Don't add new elements if queue is being stopped
		delete element;
		return true;
	}

	lock_guard<mutex> loadLock(m_qMutex);

	m_queue.push(element);

#ifdef QUEUE_DEBUG_DATA
	m_logger->debug("Element added to queue, asset [" + element->getAssetName() + \
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
	bool doProcess = true;

	while (doProcess)
	{
		NotificationQueueElement* data = NULL;
		// Get data from the queue
		{
			unique_lock<mutex> sendLock(m_qMutex);
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
				m_queue.pop();
			}
		}

		if (data)
		{
			data->queuedTimeCheck();
			// Process data
			this->processDataSet(data);
			delete data;
		}

#ifdef QUEUE_DEBUG_DATA
		m_logger->debug("Queue processing done: "
				"queue has %ld elements",
				m_queue.size());
#endif
	}

#ifdef QUEUE_DEBUG_DATA
		m_logger->debug("Queue stopped: size %ld elments",
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

	// (1) feed all rule buffers
	if (this->feedAllDataBuffers(data))
	{
		// (2) process all data in all rule buffers for given assetName
		this->processAllDataBuffers(data->getAssetName());
	}
}

/**
 * Append input data in ALL process data buffers which need assetName
 * assetName has some rules associated: ruleA, ... ruleN
 * Append same data in buffera[ruleA][assetName] ... buffera[ruleN][assetName]
 *
 * @param    data	Current item in the queue
 */
bool NotificationQueue::feedAllDataBuffers(NotificationQueueElement* data)
{
	if (!data)
	{
		return false;
	}
	bool ret = false;

	// Get assetName in the data element
	string assetName = data->getAssetName();

	// Get all subscriptions related the asetName
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	if (!subscriptions)
	{
		return false;
	}

	// Get NotificationManager instance
	NotificationManager* manager = NotificationManager::getInstance();

	subscriptions->lockSubscriptions();
	std::vector<SubscriptionElement>&
		subscriptionItems = subscriptions->getSubscription(assetName);

	for (auto it = subscriptionItems.begin();
		  it != subscriptionItems.end();
		  ++it)
	{
		lock_guard<mutex> guard(manager->m_instancesMutex);

		// Get notification instance name
		string notificationName = (*it).getNotificationName();
		// Get instance pointer
		NotificationInstance* instance = manager->getNotificationInstance(notificationName);
		
		if (instance &&
		    instance->isEnabled())
		{
			// Get ruleName for the assetName
			string ruleName = instance->getRule()->getName();
			// Feed buffer[ruleName][theAsset] with Readings data
			ret = this->feedDataBuffer(ruleName,
						   assetName,
						   data->getAssetData());
		}
		else
		{
			if (instance)
			{
				if (instance->isZombie())
				{
					Logger::getLogger()->debug("Notification %s has Zombie instance for asset %s",
						       		   notificationName.c_str(),
							   	   assetName.c_str());
				}
			}
			else
			{
				Logger::getLogger()->debug("Notification %s has no instance for asset %s",
					       		   notificationName.c_str(),
						   	   assetName.c_str());
			}
		}
	}
	subscriptions->unlockSubscriptions();

	/*
	 * Now collect all pending deletes of notification instances
	 * and really delete them. We defer this until we know we are not
	 * processing any of the noptifications.
	 */
	manager->collectZombies();

	return ret;
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
	vector<Reading *> readings = assetData->getAllReadings();
	vector<Reading *> newReadings;
	// Create a ReadingSet deep copy
	for (auto it = readings.cbegin(); it != readings.cend(); it++)
	{
		newReadings.push_back(new Reading(**it));
	}
	ReadingSet* readingsCopy = new ReadingSet;
	readingsCopy->append(newReadings);

	NotificationDataElement* newdata = new NotificationDataElement(ruleName,
								       assetName,
								       readingsCopy);
	if (!newdata)
	{
		return false;
	}

	// Append data
	lock_guard<mutex> guard(m_bufferMutex);
	NotificationDataBuffer& dataContainer = this->m_ruleBuffers[ruleName];
	dataContainer.append(assetName, newdata);

	Logger::getLogger()->debug("Feeding buffer[%s][%s] ...",
				   ruleName.c_str(),
				   assetName.c_str());

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
		  it != data.end(); removed++)
	{
		if (data.size() <= num)
		{
			break;
		}

		// Free object data
		delete(*it);
		//Remove current vector object
		it = data.erase(it);
	}
	
#ifdef QUEUE_DEBUG_DATA
	m_logger->debug("Keeping Buffers for " + \
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
bool NotificationQueue::processDataBuffer(map<string, AssetData>& results,
					  const string& ruleName,
					  const string& assetName,
					  NotificationDetail& info)
{
#ifdef QUEUE_DEBUG_DATA
	assert(assetName.compare(info.getAssetName()) == 0);
	assert(ruleName.compare(info.getRuleName()) == 0);
#endif

	m_bufferMutex.lock();
	// Get all data for assetName in the buffer[ruleName]
	vector<NotificationDataElement*>& readingsData =
		this->getBufferData(ruleName, assetName);
	m_bufferMutex.unlock();

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
 */
void NotificationQueue::evalRule(map<string, AssetData>& results,
				 NotificationRule* rule)
{
	// Output data string for MIN/MAX/AVG/ALL DATA
	map<string, string> JSONOutput;
	// Points in time data for all SingleItem assets data
	std::multimap<uint64_t, Reading*> singleItem;

	map<string, bool> assets;
	// Build output data and Points in time data
	for (auto mm = results.begin();
		  mm != results.end();
		  ++mm)
	{
		if ((*mm).second.type != EvaluationType::EVAL_TYPE::SingleItem)
		{
			// Set output string
			JSONOutput[(*mm).first] = (*mm).second.sData;
		}
		else
		{
			// Get all readings
			for (auto r = (*mm).second.rData.begin();
				  r != (*mm).second.rData.end();
				  ++r)
			{
				// Get Reading timestamp with microseconds
				struct timeval tVal;
				(*r)->getTimestamp(&tVal);

				// Add data with microseconds timestamp as key
				std::pair<uint64_t,  Reading *> rPair =
					std::make_pair((tVal.tv_sec * 1000000 + tVal.tv_usec), (*r));

				singleItem.insert(rPair);

				assets[(*r)->getAssetName()] = true;
			}
		}
	}

	// No SingleItem evaluations found
	if (!singleItem.size())
	{
		string evalJSON = "{ ";
		addReadyData(JSONOutput, evalJSON);
		evalJSON += " }";

		// Call plugin_eval, plugin_reason and plugin_deliver
		deliverNotification(rule, evalJSON);
	}
	else
	{
		// Deliver SingleItem data + ready data
		deliverData(rule, singleItem, JSONOutput);
	}

	// Clean all buffers for SingleItem data
	// NOTE:
	// for other evaluation types we have already removed
	// the right number of buffers after creating srtring data
	for (auto mm = results.begin();
		  mm != results.end();
		  ++mm)
	{
		if ((*mm).second.type == EvaluationType::EVAL_TYPE::SingleItem)
		{
			// Clear all data in buffer buffers[rule][asset]
			lock_guard<mutex> guard(m_bufferMutex);
			this->clearBufferData(rule->getName(), (*mm).first);
		}
	}
}

/**
 * Process all data buffers for a given assetName
 *
 * The assetName might belong to differen rules:
 *
 * (1) Get all rules for the given asset name
 * (2) For each rule process data for all assets belonging to the rule
 *     in rule_buffers[ruleName][assetName]
 *
 * (3) If a notification is ready, call rule plugin_eval
 *     and delivery plugin_deliver (if notification has to be sent)
 *
 * @param    assetName		Current assetName
 *				that is receiving notifications data
 */
void NotificationQueue::processAllDataBuffers(const string& assetName)
{
	// Get the subscriptions instance
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	if (!subscriptions)
	{
		return;
	}
	// Get all subscriptions for assetName
	subscriptions->lockSubscriptions();
	std::vector<SubscriptionElement>&
		registeredItems = subscriptions->getSubscription(assetName);

	// Get NotificationManager instance
	NotificationManager* manager = NotificationManager::getInstance();

	// Iterate trough subscriptions
	for (auto it = registeredItems.begin();
		  it != registeredItems.end();
		  ++it)
	{
		lock_guard<mutex> guard(manager->m_instancesMutex);

		// Per asset notification map
		map<string, AssetData> results;

		// Get notification instance name
		string notificationName = (*it).getNotificationName();
		// Get instance pointer
		NotificationInstance* instance = manager->getNotificationInstance(notificationName);

		// Check wether the instance exists and it is enabled
		if (!instance ||
		    !instance->getRule() ||
		    !instance->isEnabled())
		{
			Logger::getLogger()->debug("Skipping instance for asset %s in notification %s",
						   assetName.c_str(),
						   (*it).getNotificationName().c_str());
			// Skip this instance
			continue;
		}

		// Get ruleName for the assetName
		string ruleName = instance->getRule()->getName();

		// Get all assests belonging to current rule
		vector<NotificationDetail>& assets = instance->getRule()->getAssets();

		// Iterate trough assets
		for (auto itr = assets.begin();
			  itr != assets.end();
			  ++itr)
		{
			// Process data buffer and fill results
			this->processDataBuffer(results,
						ruleName,
						(*itr).getAssetName(),
						*itr);
		}

		// Eval rule?
		if (results.size() == assets.size())
		{
			// Notification data ready: eval data and sent notification
			this->sendNotification(results, *it);
		}
	}
	subscriptions->unlockSubscriptions();
}

/**
 * Process all readings in data buffers
 * and return notification results data.
 *
 * This routine can process the last reading in the last buffer
 * or all the readings data, accordingly to rule evaluation type
 *
 * @param    info		The notification details for assetName
 * @param    readingsData	All data buffers
 * @param    results		The output result map to fill
 * @return			True if notifcation is ready to be sent,
 *				false otherwise.
 *
 */
bool NotificationQueue::processAllReadings(NotificationDetail& info,
					   vector<NotificationDataElement *>& readingsData,
					   map<string, AssetData>& results)
{
	bool evalRule = false;
	string assetName = info.getAssetName();
	string ruleName = info.getRuleName();
	// Get last object in the buffers
	auto c = readingsData.back();
	vector<Reading *>*s = c->getData()->getAllReadingsPtr();
	// Get last reading data
	auto r = s->back();
	struct timeval tm;
	// Save reading timestamp
	r->getTimestamp(&tm);

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
	case EvaluationType::SingleItem:
		results[assetName].type = info.getType();
		// Add all Reading data
		this->setSingleItemData(readingsData, results);

		// This notification is ready
		evalRule = true;

		break;

	case EvaluationType::Minimum:
	case EvaluationType::Maximum:
	case EvaluationType::Average:
	case EvaluationType::All:
	default:
		{
		// Process ALL buffers
		map<string, string> output;
		this->processAllBuffers(readingsData,
					info.getType(),
					info.getInterval(),
					output);

		if (output.size())
		{
			// This notification is ready
			evalRule = true;

			// Prepare string result per datapoint
			string content = "{ ";
			for (auto c = output.begin();
				  c != output.end();
				  ++c)
			{
				string dataPointName = (*c).first;
				content += "\"" + dataPointName + "\" : ";

				if (info.getType() == EvaluationType::All)
				{
					// Add leading "[" and trailing "]"
					content +=  "[ " + (*c).second +  " ]";
				}
				else
				{
					content += (*c).second;
				}

				if (next(c, 1) != output.end())
				{
					content += ", ";
				}
			}
			content += " }";

			// Add timestamp_assetName with reading timestamp
			content += ", \"timestamp_" + assetName + "\" : " + to_string(tm.tv_sec) + "." + to_string(tm.tv_usec);
 
			// Set result
			results[assetName].type = info.getType();
			results[assetName].sData = content;
		}
		break;
		}
	}

#ifdef QUEUE_DEBUG_DATA
	// Check
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

	// Return evaluation result
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

		// Create a JSON string with all datapoints
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

		// Just keep last buffer
		lock_guard<mutex> guard(m_bufferMutex);
		this->keepBufferData(data->getRuleName(),
				     data->getAssetName(),
				     1);

		return ret;
	}
}

/**
 * Check whether a notification can be sent
 * give current notification instance state and
 * evaluation of notification data
 *
 * @param    results		Notification data
 * @param    subscription	Current subscription
 * @return			True if the notification can be sent,
 *				false otherwise.
 */
void NotificationQueue::sendNotification(map<string, AssetData>& results,
					 SubscriptionElement& subscription)
{
	if (subscription.getInstance())
	{
		this->evalRule(results, subscription.getRule());
	}
}

/**
 * Process all data buffers
 *
 * @param    readingsData	The data buffers
 * @param    type		The rule evaluation type
 * @param    timeInterval	The time interval for data evaluation
 * @return			A map with string values which
 *				represents the notification data ready.
 *				If the map is empty notification is not ready yet.
 *				
 */
void NotificationQueue::processAllBuffers(vector<NotificationDataElement *>& readingsData,
					  EvaluationType::EVAL_TYPE type,
					  unsigned long timeInterval,
					  map<string, string>& result)
{
	bool evalRule = false;
	unsigned long first_time = 0;
	unsigned long buffersDone = 0;
	string assetName;
	string ruleName;

	// Iterate throught buffers data
	for (auto item = readingsData.begin();
		  item != readingsData.end();
		  ++item)
	{
		buffersDone++;
		// Processing data, for assetName
#ifdef QUEUE_DEBUG_DATA
		if (!assetName.empty())
		{
			assert(assetName.compare((*item)->getAssetName()) == 0);
		}
#endif
		assetName = (*item)->getAssetName();

#ifdef QUEUE_DEBUG_DATA
		if (!ruleName.empty())
		{
			assert(ruleName.compare((*item)->getRuleName()) == 0);
		}
#endif
		ruleName = (*item)->getRuleName();

		if (item == readingsData.begin())
		{
			// Mark first_time as timestamp of first data buffer
			first_time = (*item)->getTime();
		}

		if (((*item)->getTime() - first_time) > timeInterval)
		{
			// Exit from buffers loop
			evalRule = true;
			break;
		}
	}

	// Return notification data
	if (buffersDone && evalRule)
	{
		// Aggregate data in the buffers and set values in result map
		aggregateData(readingsData, buffersDone, type, result);

		// Just keep buffersDone buffers
		lock_guard<mutex> guard(m_bufferMutex);
		this->keepBufferData(ruleName,
				     assetName,
				     readingsData.size() - buffersDone);
	}
}

/**
 * Update or set datapoint value result map
 *
 * @param    result		Output map with current result values
 * @param    d			Input datapoint value
 * @param    type		Rule evaluation type
 */
void NotificationQueue::setValue(map<string, ResultData>& result,
				 Datapoint* d,
				 EvaluationType::EVAL_TYPE type)
{
	string key = d->getName();
	// Create a new datapoint value object
	DatapointValue val(d->getData());

	if (result.find(key) == result.end())
	{
		// Create a new datapoint object
		result[key].vData.push_back(new Datapoint(key, val));
	}
	else
	{
		// Update/Set datapoint value
		switch(type)
		{
			case EvaluationType::Minimum:
				setMinValue(result, key, val);
				break;
			case EvaluationType::Maximum:
				setMaxValue(result, key, val);
				break;
			case EvaluationType::Average:
				setSumValues(result, key, val);
				break;
			default:
				break;
		}
	}
}

/**
 * Set Min value in output result map
 *
 * @param    result		Output map with current result values
 * @param    key		Datapoint name
 * @param    val		Input datapoint value.
 */
void NotificationQueue::setMinValue(map<string, ResultData>& result,
				    const string& key,
				    DatapointValue& val)
{

	// Set MIN
	switch (val.getType())
	{
	case DatapointValue::T_INTEGER:
		if (val.toInt() < result[key].vData[0]->getData().toInt())
		{
			result[key].vData[0]->getData().setValue(val.toInt());
		}
		break;

	case DatapointValue::T_FLOAT:
		if (val.toDouble() < result[key].vData[0]->getData().toDouble())
		{
			result[key].vData[0]->getData().setValue(val.toDouble());
		}
		break;

	case DatapointValue::T_FLOAT_ARRAY:
	case DatapointValue::T_STRING:
	default:
		// Do nothing, use the current DatapointValue value
		result[key].vData[0]->getData() = val;
		break;
	}
}

/**
 * Set Max value in output result map
 *
 * @param    result		Output map with current result values
 * @param    key		Datapoint name
 * @param    val		Input datapoint value.
 */
void NotificationQueue::setMaxValue(map<string, ResultData>& result,
				    const string& key,
				    DatapointValue& val)
{

	// Set MAX
	switch (val.getType())
	{
	case DatapointValue::T_INTEGER:
		if (val.toInt() > result[key].vData[0]->getData().toInt())
		{
			result[key].vData[0]->getData().setValue(val.toInt());
		}
		break;

	case DatapointValue::T_FLOAT:
		if (val.toDouble() > result[key].vData[0]->getData().toDouble())
		{
			result[key].vData[0]->getData().setValue(val.toDouble());
		}
		break;

	case DatapointValue::T_FLOAT_ARRAY:
	case DatapointValue::T_STRING:
	default:
		// Do nothing, just overwirite the DatapointValue value
		result[key].vData[0]->getData() = val;
		break;
	}
}

/**
 * Update sum value in output result map
 *
 * @param    result		Output map with current result values
 * @param    key		Datapoint name
 * @param    val		Input datapoint value.
 */
void NotificationQueue::setSumValues(map<string, ResultData>& result,
				    const string& key,
				    DatapointValue& val)
{

	// Set MAX
	switch (val.getType())
	{
	case DatapointValue::T_INTEGER:
		result[key].vData[0]->getData().setValue(val.toInt() + result[key].vData[0]->getData().toInt());
		break;

	case DatapointValue::T_FLOAT:
		result[key].vData[0]->getData().setValue(val.toDouble() + result[key].vData[0]->getData().toDouble());
		break;

	case DatapointValue::T_FLOAT_ARRAY:
	case DatapointValue::T_STRING:
	default:
		// Do nothing, just overwirite the DatapointValue value
		result[key].vData[0]->getData() = val;
		break;
	}
}


/**
 * Deliver notification data
 *
 * 1) call rule "plugin_eval"
 * 2) check wether notification can be sent
 * 3) call rule "plugin_reason"
 * 4) send notification via delivery "plugin_deliver"
 * 5) update Audit log
 *
 * @param    rule	The notification rule
 * @param    data	JSON data to evaluate
 *
 */
static void deliverNotification(NotificationRule* rule,
				const string& data)
{
	// Eval notification data via ruel "plugin_eval"
	bool evalRule = rule->getPlugin()->eval(data);

	// Get instances
	NotificationManager* instances = NotificationManager::getInstance();

	// Find instance for this rule
	NotificationInstance* instance =
		instances->getNotificationInstance(rule->getNotificationName());

	// Get notification action
	bool handleRule = instance->handleState(evalRule);
	if (handleRule)
	{
		 // Call rule "plugin_reason"
		string reason = rule->getPlugin()->reason();

		// Call delivery "plugin_deliver"
		DeliveryPlugin* plugin = instance->getDeliveryPlugin();

		if (plugin &&
		    !plugin->isEnabled())
		{
			Logger::getLogger()->warn(
				"Notification %s has triggered but delivery plugin '%s' is not enabled",
				  rule->getNotificationName().c_str(), plugin->getName().c_str());
			return;
		}

		if (!plugin ||
		    !instance ||
		    !instance->isEnabled() ||
		    !instance->getDelivery())
		{
			Logger::getLogger()->error("Aborting delivery for notification '%s'",
						   rule->getNotificationName().c_str());
		}
		else
		{
			Logger::getLogger()->info("Notification %s will be delivered with reason %s",
					rule->getNotificationName().c_str(), reason.c_str());
			string customText = instance->getDelivery()->getText();
			bool retCode = plugin->deliver(instance->getDelivery()->getName(),
							instance->getDelivery()->getNotificationName(),
							reason,
							(customText.empty() ?
							"ALERT for " + rule->getName() :
							customText));
			// Audit log
			instances->auditNotification(instance->getName(), reason);
			// Update sent notification statistics
			instances->updateSentStats();
		}
	}
	else
	{
		Logger::getLogger()->debug("Handle state is false for notification "
					   "'%s': not delivering notifications",
					   rule->getNotificationName().c_str());
	}
}

/**
 * Aggregate data in the buffers
 * for evaluation type Min/Max/Avg and All
 *
 * @param    readingsData	Data buffers
 * @param    size		Number of buffers to aggregate
 * @param    type		The evalaution type
 * @param    ret		Output map with data
 *				map[dataPointName] = value(s)
 */
void NotificationQueue::aggregateData(vector<NotificationDataElement *>& readingsData,
				      unsigned long num,
				      EvaluationType::EVAL_TYPE type,
				      std::map<std::string, string>& ret)
{
	std::map<std::string, ResultData> result;
	string assetName;
	string ruleName;

	unsigned long i = 0;
	unsigned long readingsDone = 0;

	// Iterate throught buffers data
	for (auto item = readingsData.begin();
		  item != readingsData.end() &&
		  i < num;
		  ++item, i++)
	{
#ifdef QUEUE_DEBUG_DATA
		if (!assetName.empty())
		{
			assert(assetName.compare((*item)->getAssetName()) == 0);
		}
#endif
		assetName = (*item)->getAssetName();

#ifdef QUEUE_DEBUG_DATA
		if (!ruleName.empty())
		{
			assert(ruleName.compare((*item)->getRuleName()) == 0);
		}
#endif
		ruleName = (*item)->getRuleName();

		// Iterate throught readings
		const std::vector<Reading *>& readings = (*item)->getData()->getAllReadings();
		for (auto r = readings.begin();
			  r != readings.end();
			  ++r)
		{
			readingsDone++;

#ifdef QUEUE_DEBUG_DATA
			assert(assetName.compare((*r)->getAssetName()) == 0);
#endif

			std::vector<Datapoint *>& data = (*r)->getReadingData();
			for (auto d = data.begin();
				  d != data.end();
				  ++d)
			{
				string key = (*d)->getName();

				if (type == EvaluationType::All)
				{
					// Keep all values for any datapoint type:
					result[key].vData.push_back((*d));
				}
				else
				{
					// Set MIN or MAX or SUM
					this->setValue(result, *d, type);
				}
			} // End of datapoints
		} // End of readings
	} // End of buffers

	// Prepare output result set
	switch(type)
	{
		case EvaluationType::All:
		case EvaluationType::Minimum:
		case EvaluationType::Maximum:
		case EvaluationType::Average:
			for (auto m = result.begin();
				  m != result.end();
				  ++m)
			{
				// Create a string with datapoint value(s)
				string content;
				// Get all datapoint values (just 1 for Min/Max/Avg)
				for (auto& v: ((*m).second).vData)
				{
					if (!content.empty())
					{
						content.append(", ");
					}

					// Append Datapoint value for Min/Max/All
					if (type != EvaluationType::Average)
					{
						content.append(v->getData().toString());
					}
					else
					{
						// Calculate AVG
						long lVal;
						double dVal;
						// Check for INT or FLOAT
						switch(v->getData().getType())
						{
							case DatapointValue::T_INTEGER:
							lVal = v->getData().toInt();
							// Set output string
							content.append(to_string(lVal / (double)readingsDone));
							break;

						case DatapointValue::T_FLOAT:
							dVal = v->getData().toDouble();
							// Set output string
							content.append(to_string(dVal / (double)readingsDone));
							break;

						case DatapointValue::T_FLOAT_ARRAY:
						case DatapointValue::T_STRING:
						default:
							// Do nothing right now
							break;
						}

					}

					if (type != EvaluationType::All)
					{
						// Remove data
						delete v;
					}
				}

				// Set output string
				ret[(*m).first] = content;
			}
			break;

		default:
			// Empty result data is returned
			break;
	}
}

/**
 * Add all the Reading data in the notification rule buffers
 * into the per asset result map
 *
 * @param    readingsData	Vector of data buffers
 * @param    results		Output result map
 */
void NotificationQueue::setSingleItemData(vector<NotificationDataElement *>& readingsData,
					  map<string, AssetData>& results)
{

	for (auto item = readingsData.begin();
		  item != readingsData.end();
		   ++item)
	{
		const std::vector<Reading *>& readings = (*item)->getData()->getAllReadings();
		for (auto r = readings.begin();
			  r != readings.end();
			  ++r)
		{
			results[(*r)->getAssetName()].rData.push_back(*r);
		}
	}
}

/**
 * Build notification JOSN data for time aggregated data
 *
 * @param    readyData		Input map with ready  time aggregated data
 * @param    output		The output string to pass to plugin_eval
 */
void addReadyData(const map<string, string>& readyData,
		  string& output)
{
	for (auto mm = readyData.begin();
		  mm != readyData.end();
		  ++mm)
	{
		output += "\"" + (*mm).first + "\" : ";
		output += (*mm).second;
		if (next(mm, 1) != readyData.end())
		{
			output += ", " ;
		}
	}
}

/**
 * Deliver SingleItem notification data and time aggregated data
 *
 * Each SingleItem notification data + time aggregated data
 * is passed to plugin_eval -> plugin_reason -> plugin_deliver
 *
 * @param    rule		The notification rule
 * @param    itemData		Input vector with all SingleItem Reading data
 * @param    readyData		Input map with ready  time aggregated data
 */
static void deliverData(NotificationRule* rule,
			const std::multimap<uint64_t, Reading*>& itemData,
			const map<string, string>& readyData)
{
	map<string, string> values;

	// We have SingleItem data to evaluate
	string evalJSON = "{ ";

	// Fetch unique timestamp keys
	for (auto tLine = itemData.begin(), end = itemData.end();
		  tLine != end;
		  tLine = itemData.upper_bound(tLine->first))
	{
		// Get data
		auto ret = itemData.equal_range((*tLine).first);
		// Build output data
		for (auto eq = ret.first;
			  eq != ret.second;
			  ++eq)
		{
			// AssetName
			string assetName = (*eq).second->getAssetName();
			string assetValue = "\"" + assetName + "\" : { ";

			// DataPoints
			std::vector<Datapoint *>& data = (*eq).second->getReadingData();
			for (auto d = data.begin();
				  d != data.end();
				  ++d)
			{
				// Datapoint name and val
				assetValue += "\"" + (*d)->getName()  + "\" : " + (*d)->getData().toString();
				if (next(d, 1) != data.end())
				{
					assetValue += ", " ;
				}
			}
			// close datapoints
			assetValue += " }";

			// Get reading timestamp
			struct timeval tm;
			(*eq).second->getTimestamp(&tm);
			// Add timestamp_assetName with reading timestamp
			assetValue += ", \"timestamp_" + assetName + "\" : " + to_string(tm.tv_sec) + "." + to_string(tm.tv_usec);

			// Save asset value:
			// if assetName is not found in next point in time
			// we use this last saved value for the output string.
			values[assetName] = assetValue;
		}

		string output = "{ ";

		// Prepare output string
		for (auto res = values.begin();
			  res != values.end();
			  ++res)
		{
			output += (*res).second;
			if (next(res, 1) != values.end())
			{
				output += ", " ;
			}
		}

		// Add aggreagate data
		if (readyData.size())
		{
			output += ", " ;
			addReadyData(readyData, output);
		}
		output += " }" ;

		// Call plugin_eval, plugin_reason and plugin_deliver
		deliverNotification(rule, output);
	}
}
