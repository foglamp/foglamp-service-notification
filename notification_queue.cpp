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
	// NOTE:
	//
	// Notificatiion API server is down: we cannot receive any configuration change
	// so we don't need to lock subscriptions object
	//
        // Get all subscriptions for assetName
	std::map<std::string, std::vector<SubscriptionElement>>&
		registeredItems = subscriptions->getAllSubscriptions();

	// Iterate trough subscriptions
	for (auto it = registeredItems.begin();
		  it != registeredItems.end();
		  ++it)
	{
		for (auto s = (*it).second.begin();
			  s != (*it).second.end();
			  ++s)
		{
			// Get ruleName
			string ruleName = (*s).getRule()->getName();
			// Get all assests belonging to current rule
			vector<NotificationDetail>& assets = (*s).getRule()->getAssets();

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

/**
 * Add an element to the queue
 *
 * @param    element		The elemnennt to add the queue.
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

	// (1) feed al rule buffers
	if (this->feedAllDataBuffers(data))
	{
		// (2) process all data in all rule buffers for given assetName
		this->processAllDataBuffers(data->getAssetName());
	}
	else
	{
		delete data;
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
	bool ret = false;

	// Get assetName in the data element
	string assetName = data->getAssetName();

	// Get all subscriptions related the asetName
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	subscriptions->lockSubscriptions();
	std::vector<SubscriptionElement>&
		subscriptionItems = subscriptions->getSubscription(assetName);
	subscriptions->unlockSubscriptions();

	for (auto it = subscriptionItems.begin();
		  it != subscriptionItems.end();
		  ++it)
	{
		// Get subscription ruleName for the assetName
		string ruleName = (*it).getRule()->getName();

		// Feed buffer[ruleName][theAsset] with Readings data
		NotificationInstance* instance = (*it).getInstance();
		if (instance->isEnabled())
		{
			ret = this->feedDataBuffer(ruleName,
						   assetName,
						   data->getAssetData());
		}
	}

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
bool NotificationQueue::processDataBuffer(map<string, string>& results,
					  const string& ruleName,
					  const string& assetName,
					  NotificationDetail& info)
{
#ifdef QUEUE_DEBUG_DATA
	assert(assetName.compare(element.getAssetName()) == 0);
	assert(ruleName.compare(element.getRuleName()) == 0);
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
		lock_guard<mutex> guard(m_bufferMutex);
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
	// Get all subscriptions for assetName
	subscriptions->lockSubscriptions();
	std::vector<SubscriptionElement>&
		registeredItems = subscriptions->getSubscription(assetName);
	subscriptions->unlockSubscriptions();

	// Iterate trough subscriptions
	for (auto it = registeredItems.begin();
		  it != registeredItems.end();
		  ++it)
	{
		bool evalRule = false;

		// Per asset notification map
		map<string, string> results;

		// Get ruleName
		string ruleName = (*it).getRule()->getName();
		// Get all assests belonging to current rule
		vector<NotificationDetail>& assets = (*it).getRule()->getAssets();

		// Iterate trough assets
		for (auto itr = assets.begin();
			  itr != assets.end();
			  ++itr)
		{
			// Process data buffer and fill results
			evalRule = this->processDataBuffer(results,
							   ruleName,
							   (*itr).getAssetName(),
							   *itr);
		}

		// Eval rule?
		if (results.size() == assets.size())
		{
			// Notification data ready: check whether it can be sent
			bool ret = this->sendNotification(results, *it);
			if (ret)
			{
				// Get notification instance
				NotificationInstance* instance = (*it).getInstance();
				// Call rule "plugin_reason"
				string reason = (*it).getRule()->getPlugin()->reason();

				DeliveryPlugin* plugin = instance->getDeliveryPlugin();
				string customText = instance->getDelivery()->getText();

				bool retCode = plugin->deliver(instance->getDelivery()->getName(),
							       instance->getDelivery()->getNotificationName(),
							       reason,
							       (customText.empty() ?
								"ALERT for " + ruleName :
								instance->getDelivery()->getText()));
			}
		}
	}
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
		 // Just process the last buffer
		{
		string output = this->processLastBuffer(readingsData.back());
		if (!output.empty())
		{
			// This notification is ready
			evalRule = true;

			// Update data in the output buffer
			results[assetName] = output;
		}
		break;
		}

	case EvaluationType::Minimum:
	case EvaluationType::Maximum:
	case EvaluationType::Average:
	case EvaluationType::Window:
	default:
		// Process ALL buffers
		{
		map<string, string> output = this->processAllBuffers(readingsData,
								     info.getType(),
								     info.getInterval());
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

				if (info.getType() == EvaluationType::Window)
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

			// Set result
			results[assetName] = string(content);
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
bool NotificationQueue::sendNotification(map<string,string>& results,
					 SubscriptionElement& subscription)
{
	// Get notification instance
	NotificationInstance* instance = subscription.getInstance();

	// Eval notification data via ruel "plugin_eval"
	bool eval = this->evalRule(results, subscription.getRule());

	// Return send notification action
	return instance->handleState(eval);
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
map<string,string>
NotificationQueue::processAllBuffers(vector<NotificationDataElement *>& readingsData,
				     EvaluationType::EVAL_TYPE type,
				     unsigned long timeInterval)
{
	bool evalRule = false;

	map<string, string> window;

	unsigned long last_time = 0;
	map<string, Datapoint*> result;

	unsigned long readingsDone = 0;
	unsigned long buffersDone = 0;

	string assetName;
	string ruleName;

	// Iterate throught buffers data, reverse order
	for (auto item = readingsData.rbegin();
		  item != readingsData.rend();
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
		
		// Iterate throught readings, reverse order
		const std::vector<Reading *>& readings = (*item)->getData()->getAllReadings();
		for (auto r = readings.rbegin();
			  r != readings.rend();
			  ++r)
		{
			readingsDone++;

#ifdef QUEUE_DEBUG_DATA
			assert(assetName.compare((*r)->getAssetName()) == 0);
#endif

			unsigned long current_time = (*r)->getTimestamp();
			if (item == readingsData.rbegin() &&
			    r == readings.rbegin())
			{
				// Mark last time as timestamp of last reading data
				last_time = current_time;
			}

			// Iterate through datapoints
			std::vector<Datapoint *>& data = (*r)->getReadingData();
			for (auto d = data.begin();
				  d != data.end();
				  ++d)
			{
				string key = (*d)->getName();
				if (type != EvaluationType::Window)
				{
					// Set MIN or MAX or SUM
					this->setValue(result, *d, type);
				}
				else
				{
					// Keep window of values for any datapoint type:
					if (!window[key].empty())
					{
						window[key].append(", ");
					}
					// Just append the string value
					window[key].append((*d)->getData().toString());
				}
			}

			// Check whether the notification is ready
			if ((last_time - current_time) > timeInterval)
			{
				evalRule = true;
				// Exit from readings loop
				break;
			}
		}

		if (evalRule)
		{
			// Exit from buffers loop
			break;
		}
	}

	// Create result map 
	map<string, string> ret;

	// Return notification data
	if (buffersDone && evalRule)
	{
		// Just keep buffersDone buffers
		lock_guard<mutex> guard(m_bufferMutex);
		this->keepBufferData(ruleName, assetName, buffersDone);

		// Prepare output result set
	        switch(type)
		{
		case EvaluationType::Minimum:
		case EvaluationType::Maximum:
			for (auto m = result.begin();
				  m != result.end();
				  ++m)
			{
				// Prepare output string
				ret[(*m).first] = result[(*m).first]->getData().toString();

				// Remove data
				delete result[(*m).first];
			}
			return ret;
			break;

        	case EvaluationType::Average:
			for (auto m = result.begin();
				  m != result.end();
				  ++m)
			{
				long lVal;
				double dVal;
				// Check for INT or FLOAT
				switch(result[(*m).first]->getData().getType())
				{
				case DatapointValue::T_INTEGER:
					lVal = result[(*m).first]->getData().toInt();
					// Prepare output string
					ret[(*m).first] = to_string(lVal / (double)readingsDone);
					break;

				case DatapointValue::T_FLOAT:
					dVal = result[(*m).first]->getData().toDouble();
					// Prepare output string
					ret[(*m).first] = to_string(dVal / (double)readingsDone);
					break;

				case DatapointValue::T_FLOAT_ARRAY:
				case DatapointValue::T_STRING:
				default:
					// Do nothing right now
					break;
				}

				// Remove data
				delete result[(*m).first];
			}
			// Return empty data
			return ret;
			break;

		case EvaluationType::Window:
			// Return window data
			return window;
			break;

		default:
			// Return empty data
			return ret;
			break;
		}
	}
	else
	{
		// Rule cannot be evaluated right now: delete result values
		for (auto m = result.begin();
			  m != result.end();
		  ++m)
		{
			// Remove data
			delete (*m).second;
		}
	}

	// Return empty data
	return ret;
}

/**
 * Update or set datapoint value result map
 *
 * @param    result		Output map with current result values
 * @param    d			Input datapoint value
 * @param    type		Rule evaluation type
 */
void NotificationQueue::setValue(map<string, Datapoint *>& result,
				 Datapoint* d,
				 EvaluationType::EVAL_TYPE type)
{
	string key = d->getName();
	// Create a new datapoint value object
	DatapointValue val(d->getData());

	if (result.find(key) == result.end())
	{
		// Create a new datapoint object
		result[key] = new Datapoint(key, val);
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
void NotificationQueue::setMinValue(map<string, Datapoint *>& result,
				    const string& key,
				    DatapointValue& val)
{

	// Set MIN
	switch (val.getType())
	{
	case DatapointValue::T_INTEGER:
		if (val.toInt() < result[key]->getData().toInt())
		{
			result[key]->getData().setValue(val.toInt());
		}
		break;

	case DatapointValue::T_FLOAT:
		if (val.toDouble() < result[key]->getData().toDouble())
		{
			result[key]->getData().setValue(val.toDouble());
		}
		break;

	case DatapointValue::T_FLOAT_ARRAY:
	case DatapointValue::T_STRING:
	default:
		// Do nothing, use the current DatapointValue value
		result[key]->getData() = val;
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
void NotificationQueue::setMaxValue(map<string, Datapoint *>& result,
				    const string& key,
				    DatapointValue& val)
{

	// Set MAX
	switch (val.getType())
	{
	case DatapointValue::T_INTEGER:
		if (val.toInt() > result[key]->getData().toInt())
		{
			result[key]->getData().setValue(val.toInt());
		}
		break;

	case DatapointValue::T_FLOAT:
		if (val.toDouble() > result[key]->getData().toDouble())
		{
			result[key]->getData().setValue(val.toDouble());
		}
		break;

	case DatapointValue::T_FLOAT_ARRAY:
	case DatapointValue::T_STRING:
	default:
		// Do nothing, just overwirite the DatapointValue value
		result[key]->getData() = val;
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
void NotificationQueue::setSumValues(map<string, Datapoint *>& result,
				    const string& key,
				    DatapointValue& val)
{

	// Set MAX
	switch (val.getType())
	{
	case DatapointValue::T_INTEGER:
		result[key]->getData().setValue(val.toInt() + result[key]->getData().toInt());
		break;

	case DatapointValue::T_FLOAT:
		result[key]->getData().setValue(val.toDouble() + result[key]->getData().toDouble());
		break;

	case DatapointValue::T_FLOAT_ARRAY:
	case DatapointValue::T_STRING:
	default:
		// Do nothing, just overwirite the DatapointValue value
		result[key]->getData() = val;
		break;
	}
}
