/*
 * FogLAMP notification manager.
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
#include <plugin_api.h>
#include <plugin.h>
#include <iostream>
#include <string>

#include <notification_manager.h>
#include <rule_plugin.h>
#include <delivery_plugin.h>
#include <string.h>
#include "plugin_api.h"
#include <overmax_rule.h>

using namespace std;

NotificationManager* NotificationManager::m_instance = 0;

/**
 * NotificationDetail class constructor
 *
 * @param    asset	The asset name
 * @param    rule	The rule name the asset belongs to
 * @param    type	The notification evaluation type
 *	
 */
NotificationDetail::NotificationDetail(const string& asset,
				       const string& rule,
				       EvaluationType& type) :
				       m_asset(asset),
				       m_rule(rule),
				       m_value(type)
{
}

/*
 * NotificationDetail class destructor
 */
NotificationDetail::~NotificationDetail()
{}

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
 * @param    type	Notification type:
 *			"one shot", "retriggered", "toggled"
 * @param    rule	The NotificationRule for this instance
 * @param    delivery	The NotificationDelivery for this instance
 */
NotificationInstance::NotificationInstance(const string& name,
					   bool enable,
					   NotificationType type,
					   NotificationRule* rule,	
					   NotificationDelivery* delivery) :
					   m_name(name),
					   m_enable(enable),
					   m_type(type),
					   m_rule(rule),
					   m_delivery(delivery)
{
	// Set initial state for notification delivery
	m_lastSent = 0;
	m_state = NotificationInstance::StateCleared;
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
	ret << (this->isEnabled() ? "true" : "false") << ", ";
	ret << "\"type\": \"" << this->getTypeString(this->getType()) << "\", ";
	ret << "\"rule\": \"";
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
 * @param    service		Pointer to Notification service
 */
NotificationManager::NotificationManager(const std::string& serviceName,
					 ManagementClient* managerClient,
					 NotificationService* service) :
					 m_name(serviceName),
					 m_managerClient(managerClient),
					 m_service(service)
{
	NotificationManager::m_instance = this;

	// Get logger
	m_logger = Logger::getLogger();

	/**
	 * Add here all the builtin rules we want to make available:
	 */
	 this->registerBuiltinRule<OverMaxRule>("OverMaxRule");
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
		// Fetch instance configuration
		ConfigCategory instance = m_managerClient->getCategory(instances[i]->getName());

		// Get rule plugin to use
		const string rulePluginName = instance.getValue("rule");
		// Get delivery plugin to use
		const string deliveryPluginName = instance.getValue("channel");
		// Is enabled?
		bool enabled = instance.getValue("enable").compare("true") == 0 ||
			       instance.getValue("enable").compare("True") == 0;
		// Get notification type
		string notification_type;
		if (instance.itemExists("notification_type") &&
		    !instance.getValue("notification_type").empty())
		{
			notification_type = instance.getValue("notification_type");
		}
		else
		{
			m_logger->fatal("Unable to fetch Notification type "
					"in Notification instance '" + \
					instance.getName() + "' configuration.");
			return false;
		}
		NOTIFICATION_TYPE type = this->parseType(notification_type);
		if (type == NOTIFICATION_TYPE::None)
		{
			m_logger->fatal("Found unsupported Notification type '" + \
					notification_type + \
					"' in Notification instance '" + \
					instance.getName() + "' configuration.");
			return false;
		}

		// Get custom text message for delivery
		string customText = "";
		if (instance.itemExists("text"))
		{
			customText = instance.getValue("text");
		}

		if (enabled && rulePluginName.empty())
		{
			m_logger->fatal("Unable to fetch Notification Rule "
					"plugin name from Notification instance '" + \
					instance.getName() + "' configuration.");
			return false;
		}
		if (enabled && deliveryPluginName.empty())
		{
			m_logger->fatal("Unable to fetch Notification Delivery "
					"plugin name from Notification instance '" + \
					instance.getName() + "' configuration");
			return false;
		}

		// Load plugins and get new class instences 
		RulePlugin* rule = this->createRulePlugin(rulePluginName);
		DeliveryPlugin* deliver = this->createDeliveryPlugin(deliveryPluginName);
		if (rule && deliver)
		{
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
				m_logger->fatal(errMsg.c_str());
				delete rule;
				delete deliver;
				throw runtime_error(errMsg);
			}
			if (!m_managerClient->addCategory(deliveryDefConfig, true))
			{
				string errMsg("Cannot create/update '" + \
					      deliveryCategoryName + "' delivery plugin category");
				m_logger->fatal(errMsg.c_str());
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
					  type,
					  theRule,
					  theDelivery);
		}
		else
		{
			delete deliver;
			delete rule;
			this->addInstance(instance.getName(),
					  enabled,
					  type,
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
				      NOTIFICATION_TYPE type,
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
									  type,
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
		// Get instance JSON string
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
		m_logger->error("Unable to fetch rule plugin '%s' from configuration.",
				rulePluginName.c_str());
		// Failure
		return NULL;
	}

	m_logger->info("Loading rule plugin '%s'.",
		       rulePluginName.c_str());

	PluginManager* manager = PluginManager::getInstance();
	PLUGIN_HANDLE handle;
	if ((handle = manager->loadPlugin(rulePluginName,
					  PLUGIN_TYPE_NOTIFICATION_RULE)) != NULL)
	{
		// Suceess
		m_logger->info("Loaded rule plugin '%s'.",
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
		m_logger->error("Unable to fetch delivery plugin "
				"'%s' from configuration.",
				loadDeliveryPlugin.c_str());
		// Failure
		return NULL;
	}

	m_logger->info("Loading delivery plugin '%s'.",
		       loadDeliveryPlugin.c_str());

	PluginManager* manager = PluginManager::getInstance();
	PLUGIN_HANDLE handle;
	if ((handle = manager->loadPlugin(loadDeliveryPlugin,
					  PLUGIN_TYPE_NOTIFICATION_DELIVERY)) != NULL)
        {
		// Suceess
		m_logger->info("Loaded delivery plugin '%s'.",
			       loadDeliveryPlugin.c_str());
	}
	return handle;
}

/**
 * Parse the Notification type string
 *
 * @param    type	The notification type:
 *			"one shot", "retriggered", "toggled"
 * @return		The NotificationType value
 */
NOTIFICATION_TYPE NotificationManager::parseType(const string& type)
{
	NOTIFICATION_TYPE ret;
	const char* ptrType = type.c_str();

	if (strcasecmp(ptrType, "one shot") == 0 ||
	    strcasecmp(ptrType, "oneshot") == 0)
	{
		ret = NOTIFICATION_TYPE::OneShot;
	}
	else if (strcasecmp(ptrType, "toggled") == 0)
	{
		ret = NOTIFICATION_TYPE::Toggled;
	}
	else if (strcasecmp(ptrType, "retriggered") == 0)
	{
		ret = NOTIFICATION_TYPE::Retriggered;
	}
	else
	{
		ret = NOTIFICATION_TYPE::None;
	}
	return ret;
}

/**
 * Return string value of NotificationType enum
 *
 * @param    type	The NotificationType value
 * @return		String value of NotificationType value
 */
string NotificationInstance::getTypeString(NOTIFICATION_TYPE type)
{
	string ret = "";
	switch (type)
	{
		case NOTIFICATION_TYPE::OneShot:
			ret = "One Shot";	
			break;
		case NOTIFICATION_TYPE::Toggled:
			ret = "Toggled";
			break;
		case NOTIFICATION_TYPE::Retriggered:
			ret = "Retriggered";
			break;
		default:
			break;
	}
	return ret;
}

/**
 * Wraps the loading of a rule plugin and return the RulePlugin class
 *
 * @param    rulePluginName		The rule plugin to load.
 * @return   The RulePlugin class new instance or NULL on errors.
 */
RulePlugin* NotificationManager::createRulePlugin(const string& rulePluginName)
{
	RulePlugin* rule = NULL;
	PLUGIN_HANDLE handle = NULL;

	// Check for builtin rule first
	RulePlugin* isBuiltin = this->findBuiltinRule(rulePluginName);
	if (isBuiltin)
	{
		return isBuiltin;
	}
	else
	{
		handle = this->loadRulePlugin(rulePluginName);
		if (handle)
		{
			rule = new RulePlugin(rulePluginName, handle);
		}
	}

	// Return pointer to RulePlugin class instance
	return rule;
}

/**
 * Wraps the loading of a delivery plugin and return the DeliveryPlugin class
 *
 * @param    deliveryPluginName		The delivery plugin to load.
 * @return   The DeliveryPlugin class new instance or NULL on errors.
 */
DeliveryPlugin* NotificationManager::createDeliveryPlugin(const string& deliveryPluginName)
{
	DeliveryPlugin* delivery = NULL;
	PLUGIN_HANDLE handle = NULL;

	// Load the delivery plugin
	handle = this->loadDeliveryPlugin(deliveryPluginName);
	if (handle)
	{
		// Create DeliveryPlugin class instance
		delivery = new DeliveryPlugin(deliveryPluginName, handle);
	}
	// Return pointer to DeliveryPlugin class instance
	return delivery;
}

/**
 * Find the builtin rule
 *
 * @param    ruleName
 * @return   True if the ruleName is a builtin one, false otherwise
 */
RulePlugin* NotificationManager::findBuiltinRule(const string& ruleName)
{
	if (!m_builtinRules.size())
	{
		// No builtin rules
		return NULL;
	}

	auto it = m_builtinRules.find(ruleName);
	if (it !=  m_builtinRules.end())
	{
		// Return the class instance for ruleName
		return it->second(ruleName);
	}
	else
	{
		// ruleName not found
		return NULL;
	}
}

/**
 * Register a builtin rule class, derived form RulePlugin class
 *
 * Call this routine with the class name T and its "string" name:
 * registerBuiltinRule<OverMaxRule>("OverMaxRule");
 *
 * @param   ruleName	The built in rule name
 */
template<typename T> void
NotificationManager::registerBuiltinRule(const std::string& ruleName)
{
	m_builtinRules[ruleName] = [](const std::string& ruleName)
				   {
					return new T(ruleName);
				   };
}

/**
 * Check whether a notification can be sent
 *
 * @param    evalRet	Notification data evaluation
 *			via rule "plugin_eval" call
 * @return	True if notification has to be sent or false
 */
bool NotificationInstance::handleState(bool evalRet)
{	
	bool ret = false;
	time_t now = time(NULL);
	NotificationInstance::NotificationType type = this->getType();

	switch(type)
	{
	case NotificationInstance::Toggled:
		if (m_state == NotificationState::StateTriggered)
		{
			if (evalRet == false)
			{
				// Set cleared
				m_state = NotificationState::StateCleared;
				m_lastSent = now;
				// Notify toggled
				ret = true;
			}
			else
			{
				if ((now - m_lastSent) > DEFAULT_TOGGLE_FREQUENCY)
				{
					m_lastSent = now;
					// Notify triggered
					ret = true;
				}
				else
				{
					// Just log
				}
			}
		}
		else
		{
			if (evalRet == true)
			{
				// Set Triggered 
				m_state = NotificationState::StateTriggered;
				m_lastSent = now;
				// Notify toggled
				ret = true;
			}
		}

		break;
	
	case NotificationInstance::OneShot:
		if (m_state == NotificationState::StateTriggered)
		{
			if (evalRet == false)
			{
				// Set cleared
				m_state = NotificationState::StateCleared;
			}
			else
			{
				if ((now - m_lastSent) > DEFAULT_ONESHOT_FREQUENCY)
				{
					// Update last sent time
					m_lastSent = now;
					// Send notification
					ret = true;
				}
				else
				{
					// Just log
				}
			}
		}
		else
		{
			if (evalRet == true)
			{
				// Set triggered
				m_state = NotificationState::StateTriggered;
				// Update last sent time
				m_lastSent = now;
				// Send notification
				ret = true;
			}
		}
		break;

	case NotificationInstance::Retriggered:
		if (evalRet == true)
		{
			if (m_state == NotificationState::StateTriggered)
			{
				if ((now - m_lastSent) > DEFAULT_RETRIGGER_FREQUENCY)
				{
					// Update last sent time
					m_lastSent = now;
					// Send notification
					ret = true;
				}
				else
				{
					// Just log
				}
			}
			else
			{
				// Set triggered
				m_state = NotificationState::StateTriggered;
				// Update last sent time
				m_lastSent = now;
				// Send notification
				ret = true;
			}
		}
		else
		{
			// Set cleared state
			m_state = NotificationState::StateCleared;
		}

		break;

	default:
		break;
	}

	return ret;
}

/**
 * Return JSON string of a notification rule object
 *
 * @return      A JSON string representation of the rule
 */
string NotificationRule::toJSON()
{
	ostringstream ret;

	ret << "{\"" << this->getPlugin()->getName() << "\": ";
	ret << this->getPlugin()->getInfo()->config;
	ret << " }";

	return ret.str();
}

/**
 * Return JSON string of a notification delivery object
 *
 * @return      A JSON string representation of the delivery
 */
string NotificationDelivery::toJSON()
{
	ostringstream ret;

	ret << "{\"" << this->getPlugin()->getName() << "\": ";
	ret << this->getPlugin()->getInfo()->config;
	ret << " }";

	return ret.str();
}

/**
 * Return a JSON string with current loaded notification rules
 *
 * @return	JSON string with all loaded rules
 */
string NotificationManager::getJSONRules()
{
	string ret;
	PluginManager* plugins = PluginManager::getInstance();
	list<std::string> pList;
	plugins->getInstalledPlugins("notificationRule", pList);

	if (!pList.size())
	{
		return "{}";
	}

	bool foundPlugin = false;
	ret = "[";

	for (auto it = pList.begin();
		  it != pList.end();
		  ++it)
	{
		PLUGIN_HANDLE pHandle = plugins->findPluginByName(*it);
		if (pHandle)
		{
			foundPlugin = true;
			ret += this->getPluginInfo(plugins->getInfo(pHandle));
			if (std::next(it) != pList.end())
			{
				ret += ", ";
			}
		}
	}

	// Add ", " if at least one loaded plugin and one builtin rule
	if (foundPlugin && m_builtinRules.size())
	{
		ret += ", ";
	}
	
	for (auto it = m_builtinRules.begin();
		  it != m_builtinRules.end();
		  ++it)
	{
		RulePlugin* builtinRule = this->findBuiltinRule((*it).first);
		if (builtinRule)
		{
			ret += this->getPluginInfo(builtinRule->getInfo());
			if (std::next(it) != m_builtinRules.end())
			{
				ret += ", ";
			}
		}
		delete builtinRule;
	}

	ret += "]";

	return ret;
}

/**
 * Return a JSON string with current loaded notification delivery objects
 *
 * @return	JSON string with all loaded delivery objects
 */
string NotificationManager::getJSONDelivery()
{

	string ret;
	PluginManager* plugins = PluginManager::getInstance();
	list<std::string> pList;
	plugins->getInstalledPlugins("notificationDelivery", pList);

	if (!pList.size())
	{
		return "{}";
	}

	ret = "[";
	for (auto it = pList.begin();
		  it != pList.end();
		  ++it)
	{
		PLUGIN_HANDLE pHandle = plugins->findPluginByName(*it);
		if (pHandle)
		{
			ret += this->getPluginInfo(plugins->getInfo(pHandle));
			if (std::next(it) != pList.end())
			{
				ret += ", ";
			}
		}
	}

	ret += "]";

	return ret;
}

/**
 * Creates an empty, disabled notification category
 * within the Notifications parent.
 *
 * @param    name	The notification instance to create
 * @return		True on success, false otherwise
 */
bool NotificationManager::createEmptyInstance(const string& name)
{
	bool ret = false;

	// Create an empty Notification category
	string payload = "{\"name\" : {\"description\" : \"The name of this notification\", "
			 "\"type\" : \"string\", \"default\": \"Notification " + name + "\"}, ";
	payload += "\"description\" :{\"description\" : \"Description of this notification\", "
			 "\"type\": \"string\", \"default\": \"\"}, "
			 "\"rule\" : {\"description\": \"Rule to evaluate\", "
			 "\"type\": \"string\", \"default\": \"\"}, "
			 "\"channel\": {\"description\": \"Channel to send alert on\", "
			 "\"type\": \"string\", \"default\": \"\"}, "
			 "\"notification_type\": {\"description\": \"Type of notification\", \"type\": "
			 "\"enumeration\", \"options\": [ \"one shot\", \"retriggered\", \"toggled\" ], "
			 "\"default\" : \"one shot\"}, "
			 "\"enable\": {\"description\" : \"Enabled\", "
			 "\"type\": \"boolean\", \"default\": \"false\"}}";

	DefaultConfigCategory notificationConfig(name, payload);
	notificationConfig.setDescription("Notification " + name);

	// Don't update any existing configuration, just replace all 
	if (m_managerClient->addCategory(notificationConfig, false))
	{
		// Create the empty Notification instance
		this->addInstance(name,
				  false,
				  NOTIFICATION_TYPE::OneShot,
				  NULL,
				  NULL);

		try
		{
			// Add the category name under "Notifications" parent category
			vector<string> children;
			children.push_back(name);
			m_managerClient->addChildCategories("Notifications",
							    children);
			// Register category for configuration updates
			m_service->registerCategory(name);

			// Success
			ret = true;
		}
		catch (std::exception* ex)
		{
			delete ex;
		}
	}	
	return ret;
}

/**
 * Create a rule subcategory for the notification
 * with the template content for the given rule.
 *
 * @param    name	The notification name 
 * @param    rule	The notification rule to create
 * @return		True on success, false otherwise
 */
bool NotificationManager::createRuleCategory(const string& name,
					     const string& rule)
{
        bool ret = true;
	RulePlugin* rulePlugin = this->createRulePlugin(rule);

	if (!rulePlugin)
	{
		return false;
	}

	// Create category names for plugins under instanceName
	// with names: "rule" + instanceName
	string ruleCategoryName = "rule" + name;

	// Get plugins default configuration
	string rulePluginConfig = rulePlugin->getInfo()->config;

	DefaultConfigCategory ruleDefConfig(ruleCategoryName,
					    rulePluginConfig);

	// Create category, don't merge existing values
	if (!m_managerClient->addCategory(ruleDefConfig, false))
	{
		string errMsg("Cannot create/update '" + \
			      ruleCategoryName + "' rule plugin category");
		m_logger->fatal(errMsg.c_str());

		ret = false;
	}

	try
	{
		// Add ruleCategoryName as child of Notification name
		vector<string> children;
		children.push_back(ruleCategoryName);
		m_managerClient->addChildCategories(name, children);

		// Register category for configuration updates
		m_service->registerCategory(ruleCategoryName);
	}
	catch (std::exception* ex)
	{
		delete ex;
		ret = false;
	}

	return ret;
}

/**
 * Create a delivery subcategory for the notification
 * with the template content for the given delivery plugin.
 *
 * @param    name	The notification name 
 * @param    delivery	The notification delivery to create
 * @return		True on success, false otherwise
 */
bool NotificationManager::createDeliveryCategory(const string& name,
						 const string& delivery)
{
        bool ret = true;

	DeliveryPlugin* deliveryPlugin = this->createDeliveryPlugin(delivery);

	if (!deliveryPlugin)
	{
		return false;
	}

	// Create category names for plugins under instanceName
	// with names: "delivery" + instanceName
	string deliveryCategoryName = "delivery" + name;

	// Get plugins default configuration
	string deliveryPluginConfig = deliveryPlugin->getInfo()->config;

	DefaultConfigCategory deliveryDefConfig(deliveryCategoryName,
						deliveryPluginConfig);

	// Create category, don't merge existing values
	if (!m_managerClient->addCategory(deliveryDefConfig, false))
	{
		string errMsg("Cannot create/update '" + \
			      deliveryCategoryName + "' delivery plugin category");
		m_logger->fatal(errMsg.c_str());

		ret = false;
	}

	try
	{
		// Add ruleCategoryName as child of Notification name
		vector<string> children;
		children.push_back(deliveryCategoryName);
		m_managerClient->addChildCategories(name, children);

		// Register category for configuration updates
		m_service->registerCategory(deliveryCategoryName);
	}
	catch (std::exception* ex)
	{
		delete ex;
		ret = false;
	}

	return ret;
}

/**
 * Reconfigure a notification instance
 *
 * NOTE: not yet implemented
 *
 * @param    name		The notification to reconfigure
 * @param    category		The JSON string with new configuration
 * @return			True on success, false otherwise.
 */
bool NotificationInstance::reconfigure(const string&name,
					const string& category)
{
	return true;
}

/**
 * Return JSON string with pluginInfo data
 *
 * @param    info	The plugin info C API 
 * @return		The JSON info string
 */
string NotificationManager::getPluginInfo(PLUGIN_INFORMATION* info)
{
	string ret;
	if (!info)
	{
		ret = "{}";
	}
	else
	{
		ret += "{\"name\": \"" + string(info->name) + "\", \"version\": \"" + \
			string(info->version) + "\", \"type\": \"" + string(info->type) + \
			"\", \"interface\": \"" + string(info->interface) + \
			"\", \"config\": " + string(info->config) + "}";
	}
	return ret;
}

/**
 * Create a notification instance
 *                                       
 * NOTE: not yet implemented
 *      
 * @param    name		The notification to create
 * @param    category		The JSON string with configuration
 * @return			True on success, false otherwise.
 */
bool NotificationManager::createInstance(const string& name,
					 const string& category)
{
	return true;
}

/**
 * Audit log entry for sent notification
 *
 * @param       notificationName	The notification just delivered
 * @return				True on success, false otherwise
 */
bool NotificationManager::auditNotification(const string& notificationName)
{
	return m_managerClient->addAuditEntry("NTFSN",
					      "INFORMATION",
					      "{\"name\": \"" + notificationName + "\"}");
}
