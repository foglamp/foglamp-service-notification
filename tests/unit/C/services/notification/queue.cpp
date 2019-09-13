#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

/**
 * Notification service main entry point
 */
TEST(NotificationService, Queue)
{
EXPECT_EXIT({
	string myName = "myName";

	NotificationApi* api = new NotificationApi(0, 1);
	api->setCallBackURL();

	NotificationQueue* queue = new NotificationQueue(myName);
	api->queueNotification("FOOBAR", "{\"readings\" : []}");

	// Allow queue to start
	sleep(1);

	api->stop();
        queue->stop();

	delete queue;
	delete api;

	exit(0); }, ::testing::ExitedWithCode(0), "");
}
