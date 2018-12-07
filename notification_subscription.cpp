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
}

/*
 * Destructor for the NotificationSubscription class
 */
NotificationSubscription::~NotificationSubscription()
{
	// Get NotificationAPI instance
	NotificationApi* api = NotificationApi::getInstance();
	// Get callback URL
	string callBackURL = api->getCallBackURL();

	// Get all NotificationSubscriptions
	std:map<std::string,
		std::vector<SubscriptionElement>>& subscriptions = this->getAllSubscriptions();

	for (auto it = subscriptions.begin();
		  it != subscriptions.end();
		  ++it)
	{
		// Unregister interest
		m_storage.unregisterAssetNotification((*it).first,
						      callBackURL + (*it).first);

		Logger::getLogger()->info("Unregistering asset '" + \
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
	std::map<std::string, NotificationInstance *>& instances = manager->getInstances();

	for (auto it = instances.begin();
		  it != instances.end();
		  ++it)
	{
		// Get asset names from plugin_triggers call
		NotificationInstance* instance = (*it).second;
		if (!instance)
		{
			Logger::getLogger()->error("Notification instance %s is NULL",
						   (*it).first.c_str());
			continue;
		}

		if (!instance->isEnabled())
		{
			Logger::getLogger()->info("Notification instance %s is not enabled.",
						  (*it).first.c_str());
			continue;
		}

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
				Logger::getLogger()->error("Failed to parse %s plugin_triggers JSON data",
							   rulePluginInstance->getName().c_str());
				continue;
			}

			const Value& triggers = JSONData["triggers"];
			if (!triggers.Size())
			{
				Logger::getLogger()->info("No triggers set for %s plugin",
							  rulePluginInstance->getName().c_str());
				continue;
			}

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
								 (*it).first,
								 instance);

				// Add subscription and register asset interest
				bool ret = this->addSubscription(asset, subscription);
			}
		}
	}
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
		Logger::getLogger()->fatal("Error while registering asset '" + \
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

		Logger::getLogger()->info("Registering asset '" + \
					  assetName + "' for notification " + \
					  element.getNotificationName());
	}

	Logger::getLogger()->info("Subscription for asset '" + assetName + \
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
