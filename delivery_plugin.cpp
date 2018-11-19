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
				     const string& message))
				     manager->resolveSymbol(handle,
							    "plugin_deliver");

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

bool DeliveryPlugin::deliver(const std::string message)
{
	bool ret = false;
	if (this->pluginDeliverPtr)
	{
		ret = this->pluginDeliverPtr(m_instance, message);
	}
	return ret;
}
