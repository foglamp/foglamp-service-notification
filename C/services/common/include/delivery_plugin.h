#ifndef _DELIVERY_PLUGIN_H
#define _DELIVERY_PLUGIN_H
/*
 * Fledge Delivery plugin class.
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
		bool			ingestData() { return info->options & SP_INGEST; };
		void			start();
		bool			deliver(const std::string& deliveryName,
						const std::string& notificationName,
						const std::string& triggerReason,
						const std::string& customText);
		void				reconfigure(const std::string& newConfig);
		void			registerIngest(void *func, void *data);
		bool			isEnabled() { return m_enabled; };

	private:
		PLUGIN_HANDLE		(*pluginInit)(const ConfigCategory* config);
		void			(*pluginShutdownPtr)(PLUGIN_HANDLE);
		bool			(*pluginDeliverPtr)(PLUGIN_HANDLE,
							    const std::string&,
							    const std::string&,
							    const std::string&,
							    const std::string&);
		void			(*pluginReconfigurePtr)(PLUGIN_HANDLE,
							        const std::string& newConfig);
		void			setEnabled(const ConfigCategory& config);

	public:
		// Persist plugin data
		PluginData*     	m_plugin_data;

	private:
		std::string     	m_name;
		PLUGIN_HANDLE   	m_instance;
		std::mutex		m_configMutex;
		bool			m_enabled;
};

#endif
