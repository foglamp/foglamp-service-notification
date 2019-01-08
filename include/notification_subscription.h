#ifndef _NOTIFICATION_SUBSCRIPTION_H
#define _NOTIFICATION_SUBSCRIPTION_H
/*
 * FogLAMP notification subscription manager.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <logger.h>
#include <management_client.h>
#include <storage_client.h>
#include <notification_manager.h>

/**
 * The SubscriptionElement class handles the notification registration to
 * storage server based on asset name and its notification name.
 */
class SubscriptionElement
{
	public:
		SubscriptionElement(const std::string& assetName,
				    const std::string& notificationName,
				    NotificationInstance* notification);

		~SubscriptionElement();

		const std::string&	getAssetName() const { return m_asset; };
		const std::string&	getNotificationName() const { return m_name; };
		NotificationRule*	getRule()
		{
			if (m_notification)
				return m_notification->getRule();
			else
				return NULL;
		};
		NotificationDelivery*	getDelivery() { return m_notification->getDelivery(); };
		NotificationInstance*	getInstance() { return m_notification; };

	private:
		std::string	m_asset;
		std::string	m_name;
		NotificationInstance*	m_notification;
};

/**
 * The NotificationSubscription class handles all notification registrations to
 * storage server.
 * Registrations are done per asset name and one asset name might have different
 * notification rules.
 */
class NotificationSubscription
{
	public:
		NotificationSubscription(const std::string& notificationName,
					 StorageClient& storageClient);
		~NotificationSubscription();

		static	NotificationSubscription*
					getInstance() { return m_instance; };
		void			registerSubscriptions();
		void			unregisterSubscriptions();
		const std::string&	getNotificationName() { return m_name; };
		std::map<std::string, std::vector<SubscriptionElement>>&
					getAllSubscriptions() { return m_subscriptions; };
		std::vector<SubscriptionElement>&
					getSubscription(const std::string& assetName)
		{
			return m_subscriptions[assetName];
		};
		bool 			addSubscription(const std::string& assetName,
							SubscriptionElement& element);
		void			unregisterSubscription(const std::string& assetName);
		bool			createSubscription(NotificationInstance* instance);
		void			lockSubscriptions() { m_subscriptionMutex.lock(); };
		void			unlockSubscriptions() { m_subscriptionMutex.unlock(); };
		void			removeSubscription(const string& assetName,
							   const string& ruleName);

	private:
		EvaluationType		getEvalType(const Value& value);

	private:
		const std::string	m_name;
		static NotificationSubscription*
					m_instance;
		StorageClient&		m_storage;
		// There can be different subscriptions for the same assetName
		std::map<std::string, std::vector<SubscriptionElement>>
					m_subscriptions;
		Logger*			m_logger;
		std::mutex		m_subscriptionMutex;
};

#endif
