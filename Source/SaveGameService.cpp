#include "SaveGameService.h"
#include "GameScene.h"

#include "axmol.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace ax;

std::string SaveGameService::savePath()
{
    return FileUtils::getInstance()->getWritablePath() + "savegame.json";
}

bool SaveGameService::saveGame(GameScene* scene)
{
    if (!scene)
        return false;

    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", kSaveVersion, alloc);

    const Vec3 pos = scene->getPlayerPosition();
    doc.AddMember("playerX", pos.x, alloc);
    doc.AddMember("playerY", pos.y, alloc);
    doc.AddMember("playerZ", pos.z, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const std::string path = savePath();
    const bool ok          = FileUtils::getInstance()->writeStringToFile(buffer.GetString(), path);
    AXLOGI("SaveGameService: save {} -> {}", ok ? "OK" : "FAILED", path);
    return ok;
}

bool SaveGameService::loadGame(GameScene* scene)
{
    if (!scene || !hasSave())
        return false;

    const std::string json = FileUtils::getInstance()->getStringFromFile(savePath());

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject())
    {
        AXLOGW("SaveGameService: повреждённый файл сохранения, игнорируем");
        return false;
    }

    // Защитное чтение: при отсутствии/неверном типе поля используем 0.
    auto readFloat = [&doc](const char* key) -> float {
        return (doc.HasMember(key) && doc[key].IsNumber()) ? doc[key].GetFloat() : 0.0f;
    };

    const Vec3 pos{readFloat("playerX"), readFloat("playerY"), readFloat("playerZ")};
    scene->setPlayerPosition(pos);
    return true;
}

bool SaveGameService::hasSave()
{
    return FileUtils::getInstance()->isFileExist(savePath());
}
