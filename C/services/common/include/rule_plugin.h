#ifndef _RULE_PLUGIN_H
#define _RULE_PLUGIN_H
/*
 * Fledge Rule plugin class.
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

/**
 * Rule Plugin class
 *
 * Wrapper class for NotificationRule plugin
 */
class RulePlugin : public Plugin
{
	public:
		RulePlugin(const std::string& name,
			   PLUGIN_HANDLE handle);
	        virtual ~RulePlugin();

		const std::string		getName() const { return m_name; };
		virtual PLUGIN_HANDLE		init(const ConfigCategory& config);
		virtual void			shutdown();
		virtual bool			persistData() const { return info->options & SP_PERSIST_DATA; };
		virtual std::string		triggers();
		virtual bool			eval(const std::string& assetValues);
		virtual std::string		reason() const;
		virtual bool			isBuiltin() const { return false; };
		virtual PLUGIN_INFORMATION*	getInfo();
		virtual void			reconfigure(const std::string& newConfig);

	private:
		PLUGIN_HANDLE			(*pluginInit)(const ConfigCategory* config);
		void				(*pluginShutdownPtr)(PLUGIN_HANDLE);
		std::string			(*pluginTriggersPtr)(PLUGIN_HANDLE);
		bool				(*pluginEvalPtr)(PLUGIN_HANDLE,
								 const std::string& assetValues);
		std::string			(*pluginReasonPtr)(PLUGIN_HANDLE);
		void				(*pluginReconfigurePtr)(PLUGIN_HANDLE,
									const std::string& newConfig);

	public:
		// Persist plugin data
		PluginData*     		m_plugin_data;

	protected:
		PLUGIN_HANDLE   		m_instance;
		std::mutex			m_configMutex;

	private:
		std::string     		m_name;
};

#endif
