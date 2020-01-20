/**
 * FogLAMP delivery plugin class
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <delivery_plugin.h>

using namespace std;


// DeliveryPlugin constructor
DeliveryPlugin::DeliveryPlugin(const std::string& name,
			       PLUGIN_HANDLE handle) :
			       Plugin(handle), m_name(name)
{
	// Setup the function pointers to the plugin
	pluginInit = (PLUGIN_HANDLE (*)(const ConfigCategory *))
					manager->resolveSymbol(handle,
							       "plugin_init");
	pluginShutdownPtr = (void (*)(PLUGIN_HANDLE))
				      manager->resolveSymbol(handle,
							     "plugin_shutdown");

	pluginDeliverPtr = (bool (*)(const PLUGIN_HANDLE,
				     const string& deliveryName,
				     const string& notificationName,
				     const string& triggerReason,
				     const string& message))
				     manager->resolveSymbol(handle,
							    "plugin_deliver");

	pluginReconfigurePtr = (void (*)(PLUGIN_HANDLE, const std::string&))
					 manager->resolveSymbol(handle,
								"plugin_reconfigure");

	pluginStartPtr = (void (*)(PLUGIN_HANDLE))
				   manager->resolveSymbol(handle,
							  "plugin_start");

	// Persist data initialised
	m_plugin_data = NULL;

	// Set disable
	m_enabled = false;
}

//DeliveryPlugin destructor
DeliveryPlugin::~DeliveryPlugin()
{
	delete m_plugin_data;
}

PLUGIN_HANDLE DeliveryPlugin::init(const ConfigCategory& config)
{
	m_instance = this->pluginInit(&config);
	// Set the enable flag
	this->setEnabled(config);
	// Return instance
	return (m_instance ? &m_instance : NULL);
}

/**
 * Register a function that the plugin canm call to ingest a reading
 *
 * @param func	The function to call
 * @param data	First argument to pass t above function
 */
void DeliveryPlugin::registerIngest(void *func, void *data)
{
void (*pluginRegisterPtr)(PLUGIN_HANDLE, void *, void *) =
       			(void (*)(PLUGIN_HANDLE, void *, void *))
			      manager->resolveSymbol(handle, "plugin_registerIngest");
	if (pluginRegisterPtr)
		(*pluginRegisterPtr)(m_instance, func, data);
}

/**
 * Call the loaded plugin "plugin_shutdown" method
 */
void DeliveryPlugin::shutdown()
{
	if (this->pluginShutdownPtr)
	{
		return this->pluginShutdownPtr(m_instance);
	}
}

/**
 * Call the loaded plugin "plugin_deliver" method
 *
 * @param    deliveryName	The category name associated to the plugin.
 * @param    notificationName	The notification name this delivery plugin
 *				instance belongs to.
 * @param    triggerReason	A JSON string with the related
 *				triggered rule reason.
 * @param    message		A custom text message.
 * @return			True on success, false otherwise.
 */
bool DeliveryPlugin::deliver(const std::string& deliveryName,
			     const std::string& notificationName,
			     const std::string& triggerReason,
			     const std::string& message)
{
	bool ret = false;
	time_t	start = time(0);
	if (this->pluginDeliverPtr)
	{
		ret = this->pluginDeliverPtr(m_instance,
					     deliveryName,
					     notificationName,
					     triggerReason,
					     message);
	}
	unsigned int duration = time(0) - start;
	if (duration > 5)
	{
		Logger::getLogger()->warn("Delivery of notification %s was slow, %d seconds",
				notificationName.c_str(), duration);
	}
	return ret;
}

/**
 * Call the reconfigure method in the plugin
 *
 * @param    newConfig		The new configuration for the plugin
 */
void DeliveryPlugin::reconfigure(const string& newConfig)
{
	if (this->pluginReconfigurePtr)
	{
		ConfigCategory reconfig("new_cfg", newConfig);
		this->setEnabled(reconfig);

		return this->pluginReconfigurePtr(m_instance, newConfig);
	}
}

/**
 * Set the enable flag from configuration
 *
 * @param    config	The configuration of the plugin
 */
void DeliveryPlugin::setEnabled(const ConfigCategory& config)
{
	// Set the enable flag
	if (config.itemExists("enable"))
	{
		m_enabled = config.getValue("enable").compare("true") == 0 ||
			    config.getValue("enable").compare("True") == 0;

		Logger::getLogger()->debug("DeliveryPlugin::setEnabled = %d",
					   m_enabled);
	}
}

/**
 * Call plugin_start
 */
void DeliveryPlugin::start()
{
	if (pluginStartPtr != NULL)
	{
		this->pluginStartPtr(m_instance);
	}
}

/**
 * Register service
 *
 * @param func  The function to call
 * @param data  First argument to pass to above function
 */
void DeliveryPlugin::registerService(void *func, void *data)
{
	void (*pluginRegisterService)(PLUGIN_HANDLE, void *, void *) =
		(void (*)(PLUGIN_HANDLE, void *, void *))
		manager->resolveSymbol(handle, "plugin_registerService");

	if (pluginRegisterService != NULL)
	{
		(*pluginRegisterService)(m_instance, func, data);
	}
}
