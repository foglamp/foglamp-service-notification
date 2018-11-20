/*
 * FogLAMP notification manager.
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
#include <notification_manager.h>
#include <rule_plugin.h>
#include <delivery_plugin.h>

using namespace std;

NotificationManager* NotificationManager::m_instance = 0;

/**
 * NotificationElement constructor
 *
 * @param    name		Element name
 * @param    notification	The notification name
 *				this element belongs to.
 */
NotificationElement::NotificationElement(const std::string& name,
					 const std::string& notification) :
					 m_name(name),
					 m_notification(notification)
{
}

/**
 * NotificationElement destructor
 */
NotificationElement::~NotificationElement()
{
}

/**
 * NotificationRule constructor
 *
 * @param   name		The notification rule name
 * @param   notification	The notification instance name
 *				for the rule name
 * @param   plugin		The loaded rule plugin
 */
NotificationRule::NotificationRule(const std::string& name,
				   const std::string& notification,
				   RulePlugin* plugin) :
				   NotificationElement(name, notification),
				   m_plugin(plugin)
{
}

/**
 * NotificationRule destructor
 */
NotificationRule::~NotificationRule()
{
	// Free plugin resources
	m_plugin->shutdown();
	delete m_plugin;
}

/**
 * NotificationDelivery constructor
 *
 * @param   name		The notification delivery name
 * @param   notification	The notification instance name
 *				for the delivery name
 * @param   plugin		The loaded delivery plugin
 */
NotificationDelivery::NotificationDelivery(const std::string& name,
					   const std::string& notification,
					   DeliveryPlugin* plugin,
					   const std::string& customText) :
					   NotificationElement(name, notification),
					   m_plugin(plugin),
					   m_text(customText)
{
}

/*
 * NotificationDelivery destructor
 */
NotificationDelivery::~NotificationDelivery()
{
	// Free plugin resources
	m_plugin->shutdown();
	delete m_plugin;
}

/**
 * NotificationInstance constructor
 *
 * @param    name	The Notification instance name
 * @param    enable	The notification is enabled or not
 * @param    rule	The NotificationRule for this instance
 * @param    delivery	The NotificationDelivery for this instance
 */
NotificationInstance::NotificationInstance(const string& name,
					   bool enable,
					   NotificationRule* rule,	
					   NotificationDelivery* delivery) :
					   m_name(name),
					   m_enable(enable),
					   m_rule(rule),
					   m_delivery(delivery)
{
}

/**
 * NotificationInstance destructor
 */
NotificationInstance::~NotificationInstance()
{
	delete m_rule;
	delete m_delivery;
}

/**
 * Return JSON string of NotificationInstance object
 *
 * @return	A JSON string representation of the instance
 */
string NotificationInstance::toJSON()
{
	ostringstream ret;

	ret << "{\"name\": \"" << this->getName() << "\", \"enable\": ";
	ret << (this->isEnabled() ? "true" : "false") << ", \"rule\": \"";
	ret << (this->getRulePlugin() ? this->getRulePlugin()->getName() : "");
	ret << "\", \"delivery\": \"";
	ret << (this->getDeliveryPlugin() ? this->getDeliveryPlugin()->getName() : "");
	ret << "\" }";

	return ret.str();
}
/**
 * Constructor for the NotificationManager class
 *
 * @param    serviceName	Notification service name
 * @param    managerClient	Pointer to ManagementClient
 */
NotificationManager::NotificationManager(const string& serviceName,
					 ManagementClient* managerClient) :
					 m_name(serviceName),
					 m_managerClient(managerClient)
{
	// Set instance
	NotificationManager::m_instance = this;
}

/**
 * NotificationManager destructor
 */
NotificationManager::~NotificationManager()
{
	// Delete each element in m_instances
	for (auto it = m_instances.begin();
		  it != m_instances.end();
		  ++it)
	{
		delete (*it).second;
	}
}

/**
 * Get the NotificationManager instance
 *
 * @return	Pointer to NotificationManager instance
 */
NotificationManager* NotificationManager::getInstance()
{
	return m_instance;
}

/**
 * Load all notification instances found in "Notifications" FogLAMP category.
 *
 * @return			True on success, false otherwise.
 * @throw runtime_error		On fatal errors.
 */
bool NotificationManager::loadInstances()
{
	// Get child categories of "Notifications"
	ConfigCategories instances = m_managerClient->getChildCategories("Notifications");

	for (int i = 0; i < instances.length(); i++)
	{
		cerr << "Current instance is " << instances[i]->getName() << endl;
		// Fetch instance configuration
		ConfigCategory instance = m_managerClient->getCategory(instances[i]->getName());

		// Get rule plugin to use
		const string rulePluginName = instance.getValue("rule");
		// Get delivery plugin to use
		const string deliveryPluginName = instance.getValue("channel");
		// Is enabled?
		bool enabled = instance.getValue("enable").compare("true") == 0 ||
			       instance.getValue("enable").compare("True") == 0;
		// Get custom text message for delivery
		string customText = "";
		if (instance.itemExists("text"))
		{
			customText = instance.getValue("text");
		}

		if (enabled && rulePluginName.empty())
		{
			Logger::getLogger()->error("Unable to fetch Notification Rule "
						   "plugin name from Notification instance '" + \
						   instance.getName() + "' configuration.");
			return false;
		}
		if (enabled && deliveryPluginName.empty())
		{
			Logger::getLogger()->error("Unable to fetch Notificvation Delivery "
						   "plugin name from Notification instance '" + \
						   instance.getName() + "' configuration");
			return false;
		}

		// Load plugins
		PLUGIN_HANDLE ruleHandle;
		PLUGIN_HANDLE deliveryHandle;

		ruleHandle = this->loadRulePlugin(rulePluginName);
		deliveryHandle = this->loadDeliveryPlugin(deliveryPluginName);

		if (ruleHandle && deliveryHandle)
		{
			// Create RulePlugin and DeliveryPlugin instances
			RulePlugin* rule = new RulePlugin(rulePluginName,
							  ruleHandle);
			DeliveryPlugin* deliver = new DeliveryPlugin(deliveryPluginName,
								     deliveryHandle);
			// Get plugins default configuration
			string rulePluginConfig = rule->getInfo()->config;
			string deliveryPluginConfig = deliver->getInfo()->config;

			// Create category names for plugins under instanceName
			// with names: "rule" + instanceName, "delivery" + instanceName
			string ruleCategoryName = "rule" + instance.getName();
			string deliveryCategoryName = "delivery" + instance.getName();

			DefaultConfigCategory ruleDefConfig(ruleCategoryName,
							    rulePluginConfig);	
			DefaultConfigCategory deliveryDefConfig(deliveryCategoryName,
								deliveryPluginConfig);

			if (!m_managerClient->addCategory(ruleDefConfig, true))
			{
				string errMsg("Cannot create/update '" + \
					      ruleCategoryName + "' rule plugin category");
				Logger::getLogger()->fatal(errMsg.c_str());
				delete rule;
				delete deliver;
				throw runtime_error(errMsg);
			}
			if (!m_managerClient->addCategory(deliveryDefConfig, true))
			{
				string errMsg("Cannot create/update '" + \
					      deliveryCategoryName + "' delivery plugin category");
				Logger::getLogger()->fatal(errMsg.c_str());
				delete rule;
				delete deliver;
				throw runtime_error(errMsg);
			}

			// Initialise plugins
			// Get up-to-date plugin configurations
			ConfigCategory ruleConfig = m_managerClient->getCategory(ruleCategoryName);
			ConfigCategory deliveryConfig = m_managerClient->getCategory(deliveryCategoryName);

			// Call rule "plugin_init" with configuration
			rule->init(ruleConfig);

			// Call delivery "plugin_init" with configuration
			deliver->init(deliveryConfig);

			// Add plugin category name under service/process config name
			vector<string> children;
			children.push_back(ruleCategoryName);
			children.push_back(deliveryCategoryName);
			m_managerClient->addChildCategories(instance.getName(),
							    children);

			// Instantiate NotificationRule and NotificationDelivery classes
			NotificationRule* theRule = new NotificationRule(ruleCategoryName,
									 instance.getName(),
									 rule);
			NotificationDelivery* theDelivery = new NotificationDelivery(deliveryCategoryName,
										     instance.getName(),
										     deliver,
										     customText);

			// Add the new instance
			this->addInstance(instance.getName(),
					  enabled,
					  theRule,
					  theDelivery);
		}
		else
		{
			this->addInstance(instance.getName(),
					  enabled,
					  NULL,
					  NULL);
		}
	}

	return true;
}

/**
 * Add an instance to the current instances
 *
 * @param    instanceName	The instance name
 * @param    enabled		Is enabled or not
 * @param    rule		Pointer to the associated NotificationRule
 * @param    delivery		Pointer to the ssociated NotificationDelivery
 */
void NotificationManager::addInstance(const string& instanceName,
				      bool enabled,
				      NotificationRule* rule,
				      NotificationDelivery* delivery)
{
	if (m_instances.find(instanceName) != m_instances.end())
	{
		// Already set
	}
	else
	{
		// Add it
		NotificationInstance* instance = new NotificationInstance(instanceName,
									  enabled,
									  rule,
									  delivery);
		m_instances[instanceName] = instance;
	}
}

/**
 * Return a JSON string with current loaded notification instances
 *
 * @return	JSON string with all loaded instances
 */
string NotificationManager::getJSONInstances() const
{
	string ret = "";
	for (auto it = m_instances.begin();
		  it != m_instances.end();
		  ++it)
	{
		ret += (it->second)->toJSON();
		if (std::next(it) != m_instances.end())
		{
			ret += ", ";
		}
	}
	return ret;
}

/**
 * Return a notification instance, given its name
 *
 * @param instanceName		The instance name to fetch
 * @return			Pointer of the found instance or
 *				NULL if not found.
 */
NotificationInstance*
NotificationManager::getNotificationInstance(const std::string& instanceName) const
{
	auto instance = m_instances.find(instanceName);
	if (instance != m_instances.end())
	{
		return (*instance).second;
	}
	else
	{
		return NULL;
	}
}

/**
 * Load a rule plugin
 * 
 * @param    rulePluginName	The rule plugin to load
 * @return			Plugin handle on success, NULL otherwise 
 *
 */
PLUGIN_HANDLE NotificationManager::loadRulePlugin(const string& rulePluginName)
{
	if (rulePluginName.empty())
	{
		Logger::getLogger()->error("Unable to fetch rule plugin '%s' from configuration.",
					   rulePluginName.c_str());
		// Failure
		return NULL;
	}

	Logger::getLogger()->info("Loading rule plugin '%s'.",
				  rulePluginName.c_str());

	PluginManager* manager = PluginManager::getInstance();
	PLUGIN_HANDLE handle;
	if ((handle = manager->loadPlugin(rulePluginName,
					  PLUGIN_TYPE_NOTIFICATION_RULE)) != NULL)
	{
		// Suceess
		Logger::getLogger()->info("Loaded rule plugin '%s'.",
					  rulePluginName.c_str());
	}
	return handle;
}

/**
 * Load a delivery plugin
 * 
 * @param    deliveryPluginName		The delivery plugin to load
 * @return				Plugin handle on success, NULL otherwise 
 *
 */
PLUGIN_HANDLE NotificationManager::loadDeliveryPlugin(const string& loadDeliveryPlugin)
{
	if (loadDeliveryPlugin.empty())
	{
		Logger::getLogger()->error("Unable to fetch delivery plugin "
					   "'%s' from configuration.",
					   loadDeliveryPlugin.c_str());
		// Failure
		return NULL;
	}

	Logger::getLogger()->info("Loading delivery plugin '%s'.",
				  loadDeliveryPlugin.c_str());

	PluginManager* manager = PluginManager::getInstance();
	PLUGIN_HANDLE handle;
	if ((handle = manager->loadPlugin(loadDeliveryPlugin,
					  PLUGIN_TYPE_NOTIFICATION_DELIVERY)) != NULL)
        {
		// Suceess
		Logger::getLogger()->info("Loaded delivery plugin '%s'.",
					  loadDeliveryPlugin.c_str());
	}
	return handle;
}
