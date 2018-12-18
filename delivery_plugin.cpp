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

	// Persist data initialised
	m_plugin_data = NULL;
}

//DeliveryPlugin destructor
DeliveryPlugin::~DeliveryPlugin()
{
	delete m_plugin_data;
}

PLUGIN_HANDLE DeliveryPlugin::init(const ConfigCategory& config)
{
	m_instance = this->pluginInit(&config);
	return (m_instance ? &m_instance : NULL);
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
	if (this->pluginDeliverPtr)
	{
		ret = this->pluginDeliverPtr(m_instance,
					     deliveryName,
					     notificationName,
					     triggerReason,
					     message);
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
		return this->pluginReconfigurePtr(m_instance, newConfig);
	}
}
