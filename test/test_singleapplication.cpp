// test/test_singleapplication.cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include "singleapplication.h"

class SingleApplicationTest : public ::testing::Test {
protected:
    int argc = 1;
    char* argv[1] = {const_cast<char*>("test")};
};

TEST_F(SingleApplicationTest, PrimaryInstanceCreation) {
    SingleApplication app(argc, argv);
    EXPECT_TRUE(app.isPrimary());
    EXPECT_FALSE(app.isSecondary());
}

TEST_F(SingleApplicationTest, SecondaryInstanceDetection) {
    SingleApplication primary(argc, argv);
    EXPECT_TRUE(primary.isPrimary());

    SingleApplication secondary(argc, argv, true);  // Allow secondary
    EXPECT_FALSE(secondary.isPrimary());
    EXPECT_TRUE(secondary.isSecondary());
}

TEST_F(SingleApplicationTest, MessagePassing) {
    SingleApplication primary(argc, argv);
    
    QByteArray receivedMessage;
    QObject::connect(&primary, &SingleApplication::receivedMessage,
        [&receivedMessage](quint32 instanceId, QByteArray message) {
            receivedMessage = message;
        });

    SingleApplication secondary(argc, argv, true);
    secondary.sendMessage("Test Message");

    // Wait for message processing
    QCoreApplication::processEvents();

    EXPECT_EQ(receivedMessage, "Test Message");
}

TEST_F(SingleApplicationTest, InstanceID) {
    SingleApplication primary(argc, argv);
    SingleApplication secondary(argc, argv, true);
    EXPECT_NE(primary.instanceId(), secondary.instanceId());
}

TEST_F(SingleApplicationTest, UserModeVsSystemMode) {
    SingleApplication userModeApp(argc, argv, false, SingleApplication::Mode::User);
    SingleApplication systemModeApp(argc, argv, false, SingleApplication::Mode::System);
    EXPECT_TRUE(userModeApp.isPrimary());
    EXPECT_TRUE(systemModeApp.isPrimary());
}