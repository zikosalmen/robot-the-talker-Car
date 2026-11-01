#ifndef ROBOT_FACES_H
#define ROBOT_FACES_H

#include "emoji_collection.h"

#include <memory>

std::shared_ptr<EmojiCollection> GetDesktopRobotFaces();
const char* MapDesktopRobotEmotion(const char* emotion);
void InstallDesktopRobotFaces();

#endif  // ROBOT_FACES_H
