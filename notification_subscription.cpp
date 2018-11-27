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
					 NotificationRule* rule,
					 NotificationDelivery* delivery) :
					 m_asset(assetName),
					 m_name(notificationName),
					 m_rule(rule),
					 m_delivery(delivery)
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
 * Also register Notification interest to Storage server for each asset name.
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
			string ruleName = instance->getRule()->getName();
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

			for (Value::ConstValueIterator itr = triggers.Begin();
						       itr != triggers.End();
						       ++itr)
			{
				string asset = (*itr)["asset"].GetString();

				// Create subscription object
				SubscriptionElement subscription(asset,
								 (*it).first,
								 ((*it).second)->getRule(),
								 ((*it).second)->getDelivery());

				// Add subscription and register asset interest
				bool ret = this->addSubscription(asset,
								 subscription);
			}
		}
	}
}

/**
 * Add a subscription object to Subscriptions
 * and register the Reading asset notification to storage service.
 *
 * Different subscription objectis can be added to
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
	m_assets[assetName].push_back(element);

	// Register once per asset Notification interest to Storage server
	if (m_assets[assetName].size() == 1)
	{
		m_storage.registerAssetNotification(assetName,
						    (callBackURL + assetName));

		Logger::getLogger()->info("Registering asset '" + \
					  assetName + "' for notification " + \
					  element.getNotificationName());
	}
	return true;
}
