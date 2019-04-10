/*
 * FogLAMP notification subscription.
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
#include <plugin_api.h>
#include <plugin.h>
#include <logger.h>
#include <iostream>
#include <string>
#include <notification_subscription.h>
#include <notification_api.h>
#include <notification_queue.h>
#include <rule_plugin.h>
#include <delivery_plugin.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace std;
using namespace rapidjson;

NotificationSubscription* NotificationSubscription::m_instance = 0;

/**
 * SubscriptionElement class constructor
 */
SubscriptionElement::SubscriptionElement(const std::string& assetName,
					 const std::string& notificationName,
					 NotificationInstance* notification) :
					 m_asset(assetName),
					 m_name(notificationName),
					 m_notification(notification)
{
}

/**
 * SubscriptionElement class destructor
 */
SubscriptionElement::~SubscriptionElement()
{}

/**
 * Constructor for the NotificationSubscription class
 */
NotificationSubscription::NotificationSubscription(const string& notificationName,
						   StorageClient& storageClient) :
						   m_name(notificationName),
						   m_storage(storageClient)
{
	// Set instance
	m_instance = this;

	// get logger
	m_logger = Logger::getLogger();
}

/*
 * Destructor for the NotificationSubscription class
 */
NotificationSubscription::~NotificationSubscription()
{
	this->getAllSubscriptions().clear();
}

/**
 * Unregister subscriptions to storage server:
 * NOTE: subscriptions object are not deleted right now.
 */
void NotificationSubscription::unregisterSubscriptions()
{
	// Get NotificationAPI instance
	NotificationApi* api = NotificationApi::getInstance();
	// Get callback URL
	string callBackURL = api->getCallBackURL();

	// Get all NotificationSubscriptions
	m_subscriptionMutex.lock();
	std:map<std::string, std::vector<SubscriptionElement>>&
		subscriptions = this->getAllSubscriptions();
	m_subscriptionMutex.unlock();

	for (auto it = subscriptions.begin();
		  it != subscriptions.end();
		  ++it)
	{
		// Unregister interest
		m_storage.unregisterAssetNotification((*it).first,
						      callBackURL + (*it).first);

		m_logger->info("Unregistering asset '" + \
			       (*it).first + "' for notification " + \
			       this->getNotificationName());
	}
}

/**
 * Populate the Subscriptions map given the asset name
 * in "plugin_triggers" call of all rule plugins belonging to
 * registered Notification rules in NotificationManager intances.
 * Register also interest to Storage server for asset names.
 */
void NotificationSubscription::registerSubscriptions()
{
	// Get NotificationManager instance
	NotificationManager* manager = NotificationManager::getInstance();
	// Get all Notification instances
	manager->lockInstances();
	std::map<std::string, NotificationInstance *>& instances = manager->getInstances();

	for (auto it = instances.begin();
		  it != instances.end();
		  ++it)
	{
		// Get asset names from plugin_triggers call
		NotificationInstance* instance = (*it).second;
		if (!instance)
		{
			m_logger->error("Notification instance %s is NULL",
					(*it).first.c_str());
			continue;
		}

		if (!instance->isEnabled())
		{
			m_logger->info("Notification instance %s is not enabled.",
				       (*it).first.c_str());
			continue;
		}

		// Create a new subscription
		bool ret = this->createSubscription(instance);
	}
	// Unlock instances
	manager->unlockInstances();
}

/**
 * Add a subscription object to Subscriptions
 * and register the Reading asset notification to storage service.
 *
 * Different subscription objects can be added to
 * to existing ones per assetName. 
 *
 * @param    assetName		The assetName to register for notifications
 * @param    element		The Subscription object to add
 *				to current subscriptions.
 * @return			True on succes, false otherwise.
 */
bool NotificationSubscription::addSubscription(const std::string& assetName,
					       SubscriptionElement& element)
{
	// Get NotificationAPI instance
	NotificationApi* api = NotificationApi::getInstance();
	// Get callback URL
	string callBackURL = api->getCallBackURL();

	if (callBackURL.empty())
	{
		m_logger->fatal("Error while registering asset '" + \
				assetName + "' for notification " + \
				element.getNotificationName() + \
				" callback URL is not set");
		return false;
	}

	/**
	 * We can have different Subscriptions for each asset:
	 * add new one into the vector
	 */
	m_subscriptions[assetName].push_back(element);

	// Register once per asset Notification interest to Storage server
	if (m_subscriptions[assetName].size() == 1)
	{
		m_storage.registerAssetNotification(assetName,
						    (callBackURL + assetName));

		m_logger->info("Registering asset '" + \
			       assetName + "' for notification " + \
			       element.getNotificationName());
	}

	m_logger->info("Subscription for asset '" + assetName + \
		       "' has # " + to_string(m_subscriptions[assetName].size()) + " rules"); 
	return true;
}

/**
 * Check for notification evaluation type in the input JSON object
 *
 * @param    value	The input JSON object 
 * @return		NotificationType object 
 */
EvaluationType NotificationSubscription::getEvalType(const Value& value)
{
	// Default is latest, so time = 0
	time_t interval = 0;
	EvaluationType::EVAL_TYPE evaluation = EvaluationType::Latest;

	if (value.HasMember("window"))
	{
		interval = value["window"].GetUint();
		evaluation = EvaluationType::Window;
	}
	else if (value.HasMember("average"))
	{
		interval = value["average"].GetUint();
		evaluation = EvaluationType::Average;
	}
	else if (value.HasMember("minimum"))
	{
		interval = value["minimum"].GetUint();
		evaluation = EvaluationType::Minimum;
	}
	else if (value.HasMember("maximum"))
	{
		interval = value["maximum"].GetUint();
		evaluation = EvaluationType::Maximum;
	}

	return EvaluationType(evaluation, interval);
}

/**
 * Unregister a single subscription from storage layer
 *
 * @param    assetName		The asset name to unregister
 */
void NotificationSubscription::unregisterSubscription(const string& assetName)
{
	// Get NotificationAPI instance
	NotificationApi* api = NotificationApi::getInstance();
	// Get callback URL
	string callBackURL = api->getCallBackURL();

	// Get all NotificationSubscriptions
	m_subscriptionMutex.lock();
	std:map<std::string, std::vector<SubscriptionElement>>&
		subscriptions = this->getAllSubscriptions();
	auto it = subscriptions.find(assetName);
	m_subscriptionMutex.unlock();

	if (it != subscriptions.end())
	{
		// Unregister interest
		m_storage.unregisterAssetNotification((*it).first,
						      callBackURL + assetName);

		m_logger->info("Unregistering asset '" + \
				assetName + "' for notification " + \
				this->getNotificationName());
	}
}

/**
 * Create a SubscriptionElement object and register interest for asset names
 *
 * @param    instance		The notification instance
 *				with already set rule and delivery plugins
 * @return			True on success, false on errors
 */
bool NotificationSubscription::createSubscription(NotificationInstance* instance)
{
	bool ret = false;
	// Get RulePlugin
	RulePlugin* rulePluginInstance = instance->getRulePlugin();
	// Get DeliveryPlugin
	DeliveryPlugin* deliveryPluginInstance = instance->getDeliveryPlugin();

	if (rulePluginInstance)
	{
		// Call "plugin_triggers"
		string document = rulePluginInstance->triggers();

		Document JSONData;
		JSONData.Parse(document.c_str());
		if (JSONData.HasParseError() ||
		    !JSONData.HasMember("triggers") ||
		    !JSONData["triggers"].IsArray())
		{
			m_logger->error("Failed to parse %s plugin_triggers JSON data %s",
					rulePluginInstance->getName().c_str(),
					document.c_str());
			return false;
		}

		const Value& triggers = JSONData["triggers"];
		if (!triggers.Size())
		{
			m_logger->info("No triggers set for %s plugin",
				       rulePluginInstance->getName().c_str());
			return false;
		}
		m_logger->info("Triggers set for %s plugin",
				       rulePluginInstance->getName().c_str());

		string ruleName = instance->getRule()->getName();
		for (Value::ConstValueIterator itr = triggers.Begin();
					       itr != triggers.End();
					       ++itr)
		{
			// Get asset name
			string asset = (*itr)["asset"].GetString();
		 	// Get evaluation type and time period
			EvaluationType type = this->getEvalType((*itr));
			// Create NotificationDetail object
			NotificationDetail assetInfo(asset,
						     ruleName,
						     type);

			// Add assetInfo to its rule
			NotificationRule* theRule = instance->getRule();
			theRule->addAsset(assetInfo);
 
			// Create subscription object
			SubscriptionElement subscription(asset,
							 instance->getName(),
							 instance);

			// Add subscription and register asset interest
			lock_guard<mutex> guard(m_subscriptionMutex);
			ret = this->addSubscription(asset, subscription);
		}
	}
	return ret;
}

/**
 * Remove a given subscription
 *
 * @param    assetName		The register assetName for notifications
 * @param    ruleName		The associated ruleName
 */
void NotificationSubscription::removeSubscription(const string& assetName,
						  const string& ruleName)
{
	// Get all instances
	NotificationManager* manager = NotificationManager::getInstance();

	// Get subscriptions for assetName
	this->lockSubscriptions();
	map<string, vector<SubscriptionElement>>&
		allSubscriptions = this->getAllSubscriptions();
	auto it = allSubscriptions.find(assetName);
	bool ret = it != allSubscriptions.end();
	this->unlockSubscriptions();

	// For the found assetName subscriptions
	// 1- Unsubscribe notification interest for assetNamme
	// 2- Remove data in buffer[ruleName][assetName]
	// 3- Remove ruleName object fot assetName
	// 4- Remove Subscription
	if (ret)
	{
		// Get Notification queue instance
		vector<SubscriptionElement>& elems = (*it).second;
		if (elems.size() == 1)
		{
		        // 1- We have only one subscription for current asset
		        // call unregister interest
		        this->unregisterSubscription(assetName);
		}

		// Get Notification queue instance
		NotificationQueue* queue =  NotificationQueue::getInstance();
		// 2- Remove all data in buffer[ruleName][assetName]
		queue->clearBufferData(ruleName, assetName);

		// 3- Check all subscriptions rules for given assetName
		for (auto e = elems.begin();
			  e != elems.end(); )
		{
			// Get notification rule object 
			string notificationName = (*e).getNotificationName();
			manager->lockInstances();
			NotificationInstance* instance = manager->getNotificationInstance(notificationName);
			manager->unlockInstances();

			if (instance &&
			    !instance->isZombie())
			{
				string currentRule = instance->getRule()->getName();
				if (currentRule.compare(ruleName) == 0)
				{
					// 3- Remove this ruleName from array
					Logger::getLogger()->debug("Notification instance %s: removed subscription %s for asset %s",
								   notificationName.c_str(),
								   currentRule.c_str(),
								   assetName.c_str());
					elems.erase(e);
				}
				else
				{
					Logger::getLogger()->debug("Notification instance %s: Not removing subscription %s for asset %s",
								   notificationName.c_str(),
								   currentRule.c_str(),
								   assetName.c_str());
					++e;
				}
			}
			else
			{
				if (!instance)
				{
					Logger::getLogger()->debug("Notification instance %s has not been found, for asset %s",
								   notificationName.c_str(),
								   assetName.c_str());
				}

				++e;
			}
		}

		// 4- Remove subscription if array is empty
		if (!elems.size())
		{
			this->lockSubscriptions();
			allSubscriptions.erase(it);
			this->unlockSubscriptions();
		}
	}
}
