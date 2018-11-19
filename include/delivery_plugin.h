#ifndef _DELIVERY_PLUGIN_H
#define _DELIVERY_PLUGIN_H
/*
 * FogLAMP Delivery plugin class.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <plugin.h>
#include <plugin_manager.h>
#include <config_category.h>
#include <management_client.h>
#include <plugin_data.h>

// DeliveryPlugin class
class DeliveryPlugin : public Plugin
{
	public:
		DeliveryPlugin(const std::string& name,
			       PLUGIN_HANDLE handle);
	        ~DeliveryPlugin();

		const std::string	getName() const { return m_name; };
		PLUGIN_HANDLE		init(const ConfigCategory& config);
		void			shutdown();
		bool			persistData() { return info->options & SP_PERSIST_DATA; };
		void			start();
		bool			deliver(const std::string message);

	private:
		PLUGIN_HANDLE		(*pluginInit)(const ConfigCategory* config);
		void			(*pluginShutdownPtr)(PLUGIN_HANDLE);
		bool			(*pluginDeliverPtr)(PLUGIN_HANDLE,
							    const std::string&);

	public:
		// Persist plugin data
		PluginData*     	m_plugin_data;

	private:
		std::string     	m_name;
		PLUGIN_HANDLE   	m_instance;
};

#endif
