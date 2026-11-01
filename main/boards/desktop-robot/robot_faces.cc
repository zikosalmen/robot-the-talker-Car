#include "robot_faces.h"

#include "lvgl_image.h"
#include "lvgl_theme.h"

#include <cstring>
#include <memory>

#define FACE_BLOB(name)                                                                            \
    extern const uint8_t _binary_##name##_png_start[] asm("_binary_" #name "_png_start");          \
    extern const uint8_t _binary_##name##_png_end[] asm("_binary_" #name "_png_end");

extern "C" {
FACE_BLOB(neutral)
FACE_BLOB(happy)
FACE_BLOB(laughing)
FACE_BLOB(funny)
FACE_BLOB(sad)
FACE_BLOB(angry)
FACE_BLOB(crying)
FACE_BLOB(loving)
FACE_BLOB(embarrassed)
FACE_BLOB(surprised)
FACE_BLOB(shocked)
FACE_BLOB(thinking)
FACE_BLOB(winking)
FACE_BLOB(cool)
FACE_BLOB(relaxed)
FACE_BLOB(delicious)
FACE_BLOB(kissy)
FACE_BLOB(confident)
FACE_BLOB(sleepy)
FACE_BLOB(silly)
FACE_BLOB(confused)
}

#undef FACE_BLOB

namespace {

struct FaceBlob {
    const char* name;
    const uint8_t* start;
    const uint8_t* end;
};

const FaceBlob kFaces[] = {
    {"neutral", _binary_neutral_png_start, _binary_neutral_png_end},
    {"happy", _binary_happy_png_start, _binary_happy_png_end},
    {"laughing", _binary_laughing_png_start, _binary_laughing_png_end},
    {"funny", _binary_funny_png_start, _binary_funny_png_end},
    {"sad", _binary_sad_png_start, _binary_sad_png_end},
    {"angry", _binary_angry_png_start, _binary_angry_png_end},
    {"crying", _binary_crying_png_start, _binary_crying_png_end},
    {"loving", _binary_loving_png_start, _binary_loving_png_end},
    {"embarrassed", _binary_embarrassed_png_start, _binary_embarrassed_png_end},
    {"surprised", _binary_surprised_png_start, _binary_surprised_png_end},
    {"shocked", _binary_shocked_png_start, _binary_shocked_png_end},
    {"thinking", _binary_thinking_png_start, _binary_thinking_png_end},
    {"winking", _binary_winking_png_start, _binary_winking_png_end},
    {"cool", _binary_cool_png_start, _binary_cool_png_end},
    {"relaxed", _binary_relaxed_png_start, _binary_relaxed_png_end},
    {"delicious", _binary_delicious_png_start, _binary_delicious_png_end},
    {"kissy", _binary_kissy_png_start, _binary_kissy_png_end},
    {"confident", _binary_confident_png_start, _binary_confident_png_end},
    {"sleepy", _binary_sleepy_png_start, _binary_sleepy_png_end},
    {"silly", _binary_silly_png_start, _binary_silly_png_end},
    {"confused", _binary_confused_png_start, _binary_confused_png_end},
};

bool IsKnownFace(const char* emotion) {
    for (const auto& face : kFaces) {
        if (strcmp(face.name, emotion) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::shared_ptr<EmojiCollection> GetDesktopRobotFaces() {
    static std::shared_ptr<EmojiCollection> collection = []() {
        auto faces = std::make_shared<EmojiCollection>();
        for (const auto& face : kFaces) {
            const size_t size = static_cast<size_t>(face.end - face.start);
            faces->AddEmoji(face.name, new LvglRawImage(const_cast<uint8_t*>(face.start), size));
        }
        return faces;
    }();
    return collection;
}

const char* MapDesktopRobotEmotion(const char* emotion) {
    if (emotion == nullptr || emotion[0] == '\0') {
        return "neutral";
    }
    if (strcmp(emotion, "smile") == 0) {
        return "happy";
    }
    if (strcmp(emotion, "idle") == 0 || strcmp(emotion, "robot_2") == 0) {
        return "neutral";
    }
    if (IsKnownFace(emotion)) {
        return emotion;
    }
    return "neutral";
}

void InstallDesktopRobotFaces() {
    auto faces = GetDesktopRobotFaces();
    auto& themes = LvglThemeManager::GetInstance();
    if (auto* light = themes.GetTheme("light")) {
        light->set_emoji_collection(faces);
    }
    if (auto* dark = themes.GetTheme("dark")) {
        dark->set_emoji_collection(faces);
    }
}
