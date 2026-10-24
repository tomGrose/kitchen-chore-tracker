#include "app/chore_json.h"

#include <ArduinoJson.h>
#include <string.h>

#include <vector>

using dateutil::NO_DAY;

namespace chorejson {

namespace {

const char *typeName(ScheduleType t)
{
    switch(t) {
    case ScheduleType::Daily:       return "daily";
    case ScheduleType::Weekdays:    return "weekdays";
    case ScheduleType::EveryNDays:  return "days";
    case ScheduleType::EveryNWeeks: return "weeks";
    case ScheduleType::Monthly:     return "monthly";
    }
    return "daily";
}

bool typeFromName(const char *name, ScheduleType &out)
{
    if(strcmp(name, "daily") == 0)         out = ScheduleType::Daily;
    else if(strcmp(name, "weekdays") == 0) out = ScheduleType::Weekdays;
    else if(strcmp(name, "days") == 0)     out = ScheduleType::EveryNDays;
    else if(strcmp(name, "weeks") == 0)    out = ScheduleType::EveryNWeeks;
    else if(strcmp(name, "monthly") == 0)  out = ScheduleType::Monthly;
    else return false;
    return true;
}

}  // namespace

std::string serialize(const ChoreData &data)
{
    JsonDocument doc;
    doc["v"] = 1;
    doc["nextId"] = data.nextId;

    // Strings are referenced, not copied, until serializeJson() below; the
    // ChoreData they live in outlives that call.
    JsonArray chores = doc["chores"].to<JsonArray>();
    for(const Chore &c : data.chores) {
        JsonObject o = chores.add<JsonObject>();
        o["id"] = c.id;
        o["name"] = c.name.c_str();
        if(!c.icon.empty()) o["icon"] = c.icon.c_str();
        o["color"] = c.color;

        JsonObject s = o["sched"].to<JsonObject>();
        s["t"] = typeName(c.schedule.type);
        switch(c.schedule.type) {
        case ScheduleType::Weekdays:
            s["mask"] = c.schedule.weekdayMask;
            break;
        case ScheduleType::EveryNDays:
        case ScheduleType::EveryNWeeks:
            s["n"] = c.schedule.interval;
            s["anchor"] = c.schedule.anchorDay;
            break;
        case ScheduleType::Monthly:
            s["day"] = c.schedule.monthDay;
            break;
        case ScheduleType::Daily:
            break;
        }

        o["due"] = c.dueMinute;
        o["start"] = c.startDay;
        if(c.lastCompletedDay != NO_DAY) {
            o["last"] = c.lastCompletedDay;
            o["lastMin"] = c.lastCompletedMinute;
        }
        if(c.coversDay != NO_DAY) o["covers"] = c.coversDay;
        if(!c.completedBy.empty()) o["by"] = c.completedBy.c_str();
        o["cs"] = c.currentStreak;
        o["bs"] = c.bestStreak;
        o["en"] = c.enabled;
    }

    JsonArray history = doc["history"].to<JsonArray>();
    for(const HistoryEntry &h : data.history) {
        JsonObject o = history.add<JsonObject>();
        o["id"] = h.choreId;
        o["d"] = h.day;
        o["m"] = h.minute;
        if(!h.by.empty()) o["by"] = h.by.c_str();
        if(h.prevCompletedDay != NO_DAY) {
            o["pd"] = h.prevCompletedDay;
            o["pm"] = h.prevCompletedMinute;
        }
        if(h.prevCoversDay != NO_DAY) o["pc"] = h.prevCoversDay;
        o["ps"] = h.prevStreak;
        o["pb"] = h.prevBest;
    }

    const size_t length = measureJson(doc);
    std::vector<char> buffer(length + 1, '\0');
    serializeJson(doc, buffer.data(), buffer.size());
    return std::string(buffer.data(), length);
}

bool parse(const char *json, size_t length, ChoreData &out)
{
    JsonDocument doc;
    if(deserializeJson(doc, json, length)) return false;
    if((doc["v"] | 0) != 1) return false;
    if(!doc["chores"].is<JsonArray>()) return false;

    ChoreData result;
    result.nextId = static_cast<uint16_t>(doc["nextId"] | 1);

    for(JsonObject o : doc["chores"].as<JsonArray>()) {
        Chore c;
        c.id = static_cast<uint16_t>(o["id"] | 0);
        c.name = o["name"] | "";
        c.icon = o["icon"] | "";
        c.color = o["color"] | 0u;

        JsonObject s = o["sched"];
        if(s.isNull()) return false;
        if(!typeFromName(s["t"] | "", c.schedule.type)) return false;
        c.schedule.weekdayMask = static_cast<uint8_t>(s["mask"] | 0);
        c.schedule.interval = static_cast<uint16_t>(s["n"] | 1);
        c.schedule.anchorDay = s["anchor"].isNull() ? NO_DAY : s["anchor"].as<int32_t>();
        c.schedule.monthDay = static_cast<uint8_t>(s["day"] | 1);

        c.dueMinute = static_cast<int16_t>(o["due"] | -1);
        c.startDay = o["start"] | 0;
        c.lastCompletedDay = o["last"].isNull() ? NO_DAY : o["last"].as<int32_t>();
        c.lastCompletedMinute = static_cast<int16_t>(o["lastMin"] | 0);
        c.coversDay = o["covers"].isNull() ? NO_DAY : o["covers"].as<int32_t>();
        c.completedBy = o["by"] | "";
        c.currentStreak = static_cast<uint16_t>(o["cs"] | 0);
        c.bestStreak = static_cast<uint16_t>(o["bs"] | 0);
        c.enabled = o["en"] | true;

        if(c.id == 0) return false;
        result.chores.push_back(c);
    }

    for(JsonObject o : doc["history"].as<JsonArray>()) {
        HistoryEntry h;
        h.choreId = static_cast<uint16_t>(o["id"] | 0);
        h.day = o["d"] | 0;
        h.minute = static_cast<int16_t>(o["m"] | 0);
        h.by = o["by"] | "";
        h.prevCompletedDay = o["pd"].isNull() ? NO_DAY : o["pd"].as<int32_t>();
        h.prevCompletedMinute = static_cast<int16_t>(o["pm"] | 0);
        h.prevCoversDay = o["pc"].isNull() ? NO_DAY : o["pc"].as<int32_t>();
        h.prevStreak = static_cast<uint16_t>(o["ps"] | 0);
        h.prevBest = static_cast<uint16_t>(o["pb"] | 0);
        result.history.push_back(h);
    }

    out = result;
    return true;
}

}  // namespace chorejson
