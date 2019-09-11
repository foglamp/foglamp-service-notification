/**
 * Fledge rule plugin class
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <rule_plugin.h>

using namespace std;

/**
 * Constructor for the class that wraps the notification rule plugin
 *
 * Enclose a set of function pointers that resolve to the loaded plugin.
 *
 * @param    name	The plugin name.
 * @param    handle	The loaded plugin handle from
 *			plugin manager.
 */
RulePlugin::RulePlugin(const std::string& name,
		       PLUGIN_HANDLE handle) : Plugin(handle), m_name(name)
{
	if (handle != NULL)
	{
		// Setup the function pointers to the plugin
		pluginInit = (PLUGIN_HANDLE (*)(const ConfigCategory *))
						manager->resolveSymbol(handle, "plugin_init");

		pluginShutdownPtr = (void (*)(PLUGIN_HANDLE))
					      manager->resolveSymbol(handle,
							     "plugin_shutdown");
		pluginTriggersPtr = (string (*)(PLUGIN_HANDLE))
						manager->resolveSymbol(handle, "plugin_triggers");

		pluginEvalPtr = (bool (*)(PLUGIN_HANDLE,
					  const string& assetValues))
					  manager->resolveSymbol(handle, "plugin_eval");

		pluginReasonPtr = (string (*)(PLUGIN_HANDLE))
					      manager->resolveSymbol(handle, "plugin_reason");

		pluginReconfigurePtr = (void (*)(PLUGIN_HANDLE, const std::string&))
						 manager->resolveSymbol(handle,
								"plugin_reconfigure");
	}
	// Persist data initialised
	m_plugin_data = NULL;
}

/**
 * RulePlugin destructor
 */
RulePlugin::~RulePlugin()
{
	// Free plugin data
	delete m_plugin_data;
}

/**
 * Call the loaded plugin "plugin_init" method
 *
 * @param config	The rule plugin configuration
 * @return		The PLUGIN_HANDLE object
 */
PLUGIN_HANDLE RulePlugin::init(const ConfigCategory& config)
{
	m_instance = this->pluginInit(&config);
	return (m_instance ? &m_instance : NULL);
}

/**
 * Call the loaded plugin "plugin_shutdown" method
 */
void RulePlugin::shutdown()
{
	if (this->pluginShutdownPtr)
	{
		return this->pluginShutdownPtr(m_instance);
	}
}

/**
 * Call the loaded plugin "plugin_triggers" method
 *
 * @return		The JSON document, as string
 *			that describes the rule triggers.
 */
string RulePlugin::triggers()
{
	string ret = "";
	if (this->pluginTriggersPtr)
	{
		ret = this->pluginTriggersPtr(m_instance);
	}
	return ret;
}

/**
 * Call the loaded plugin "plugin_eval" method
 *
 * @param assetValues	The JSON document, as string
 *			that contains the set of asset values
 *			to evaluate.
 * @return		True if the rule was triggered,
 *			false otherwise.
 */
bool RulePlugin::eval(const string& assetValues)
{
	bool ret = false;
	time_t start = time(0);
	if (this->pluginEvalPtr)
	{
		ret = this->pluginEvalPtr(m_instance, assetValues);
	}
	int duration = time(0) - start;
	if (duration > 5)
	{
		Logger::getLogger()->warn("Rule evaluation for %s was slow, %d seconds",
				m_name.c_str(), duration);
	}
	return ret;
}

/**
 * Call the loaded plugin "plugin_reason" method
 *
 * @return		JSON string with notification reason,
 */
string RulePlugin::reason() const
{
	string ret = "";
	if (this->pluginReasonPtr)
	{
		ret = this->pluginReasonPtr(m_instance);
	}
	return ret;
}

/**
 * Return PluginInfo data
 *
 * @return	Pointer to loaded plugin Info data
 */
PLUGIN_INFORMATION* RulePlugin::getInfo()
{
	// Return 'info' member of base class Plugin
	return this->info;
}

/**
 * Call the reconfigure method in the plugin
 *
 * @param    newConfig		The new configuration for the plugin
 */
void RulePlugin::reconfigure(const string& newConfig)
{
	if (this->pluginReconfigurePtr)
	{
		return this->pluginReconfigurePtr(m_instance, newConfig);
	}
}
