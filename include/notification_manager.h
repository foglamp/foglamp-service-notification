#ifndef _NOTIFICATION_MANAGER_H
#define _NOTIFICATION_MANAGER_H
/*
 * FogLAMP notification manager.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <logger.h>
#include <management_client.h>
#include <rule_plugin.h>
#include <delivery_plugin.h>

// Base Notification element class
class NotificationElement
{
	public:
		NotificationElement(const std::string& name,
				    const std::string& notification);
		~NotificationElement();
		const std::string&	getName() const { return m_name; };
		const std::string&	getNotificatioName() const { return m_notification; };

	private:
		const std::string	m_name;
		const std::string	m_notification;
};

// NotificationRule class
class NotificationRule : public NotificationElement
{
	public:
		NotificationRule(const std::string& name,
				 const std::string& notification,
				 RulePlugin* plugin);
		~NotificationRule();
		RulePlugin*		getPlugin() { return m_plugin; };	

	private:
		RulePlugin*		m_plugin;
};

// NotificationDelivery class
class NotificationDelivery : public NotificationElement
{
	public:
		NotificationDelivery(const std::string& name,
				     const std::string& notification,
				     DeliveryPlugin* plugin);
		~NotificationDelivery();
		DeliveryPlugin*		getPlugin() { return m_plugin; };

	private:
		DeliveryPlugin*		m_plugin;
};

// NotificationInstance class
class NotificationInstance
{
	public:
		NotificationInstance(const std::string& name,
				     bool enable,
				     NotificationRule* rule,
				     NotificationDelivery* delivery);

		~NotificationInstance();

		const std::string&	getName() const { return m_name; };
		NotificationRule*	getRule() const { return m_rule; };
		NotificationDelivery*	getDelivery() const { return m_delivery; };
		RulePlugin*		getRulePlugin() const { return (m_rule ? m_rule->getPlugin() : NULL); };
		DeliveryPlugin*		getDeliveryPlugin() const { return (m_delivery ? m_delivery->getPlugin() : NULL); };
		string			toJSON();
		bool			isEnabled() const { return m_enable; };

	private:
		const std::string	m_name;
		bool			m_enable;
		NotificationRule*	m_rule;
		NotificationDelivery*	m_delivery;
};

/**
 * The NotificationManager class.
 */
class NotificationManager
{
	public:
		NotificationManager(const std::string& notificationName,
				    ManagementClient* managerClient);
		~NotificationManager();

		const std::string&	getName() const { return m_name; };
		static NotificationManager*
					getInstance();
		std::string		getJSONInstances() const;
		bool 			loadInstances();
		std::map<std::string, NotificationInstance *>&
					getInstances() { return m_instances; };
		NotificationInstance*	getNotificationInstance(const std::string& instanceName) const;

	private:
		void			addInstance(const string& instanceName,
						    bool enable,
						    NotificationRule* rule,
						    NotificationDelivery* delivery);

	private:
		const std::string	m_name;
		static NotificationManager*
					m_instance;
		ManagementClient* 	m_managerClient;
		std::map<std::string, NotificationInstance *>
					m_instances;
};
#endif
