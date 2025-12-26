#pragma once
#include <libxml/tree.h>
#include <vector>
#include <map>
#include <string>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <system/graphics.h>

// Node
#define XML_ELEM_STRING_NODE_PQ_MODES "PQ_modes"
#define XML_ELEM_STRING_NODE_MODE "Mode"

// Attribute
#define XML_ELEM_STRING_ATTR_NAME "Name"
#define XML_ELEM_STRING_ATTR_COLOR_SPACE "ColorSpace"
#define XML_ELEM_STRING_ATTR_RENDER_INTENT "RenderIntent"
#define XML_ELEM_STRING_ATTR_DYNAMIC_RANGE "DynamicRange"
#define XML_ELEM_STRING_ATTR_MODE_ID "ModeID"

// Attribute value
enum XML_ATTR_VALUE_DYNAMIC_RANGE_ENUM
{
    XML_ATTR_VALUE_DYNAMIC_RANGE_SDR = 0,
    XML_ATTR_VALUE_DYNAMIC_RANGE_HDR,
    XML_ATTR_VALUE_DYNAMIC_RANGE_MAX,
};

struct PqModeInfo
{
    int32_t id;
    int32_t color_mode;
    int32_t intent;
    int32_t dynamic_range;
    std::string name;
};

class PqXmlParser
{
public:
    PqXmlParser();
    ~PqXmlParser();

    // can we find the PQ XML in the system
    bool hasXml();

    // get the render intent list of assigned color mode
    const std::vector<PqModeInfo>& getRenderIntent(int32_t color_mode);

private:
    // read the PQ XML, and parse it
    void init();

    // parse the PQ XML via its root
    bool parseXml(xmlDoc* doc);

    // find the target node from XML tree
    xmlNode* findTargerNode(xmlNode* root, xmlElementType type, const char* name);

    // find the target node from XML tree
    xmlNode* findNextTargetNode(xmlNode* parent, xmlNode* prev, xmlElementType type,
            const char* name);

    // get the detail of PQ mode info from assigned node
    bool getPqModeInfo(xmlNode* node, PqModeInfo* info);

    // get the node's attribute
    const char* getTargetAttribute(xmlNode* node, xmlElementType type, const char* name);

    // check the node whether it has assigned type and name
    bool isTargetNode(xmlNode* node, xmlElementType type, const char* name);

    // check the attribute whether it has assigned type and name
    bool isTargetAttribute(xmlAttr* attr, xmlElementType type, const char* name);

private:
    // has PQ XML or not
    bool m_has_xml;

    // render intent table of each color mode
    std::map<int32_t, std::vector<PqModeInfo>> m_color_mode_with_render_intent;

    // mapping array for dynamic range
    const std::pair<int32_t, std::string> m_dynamic_range_table[XML_ATTR_VALUE_DYNAMIC_RANGE_MAX] = {
        {XML_ATTR_VALUE_DYNAMIC_RANGE_SDR, std::string("sdr")},
        {XML_ATTR_VALUE_DYNAMIC_RANGE_HDR, std::string("hdr")},
    };

    // mapping array for color mode
    const std::pair<int32_t, std::string> m_color_mode_table[3] = {
        {HAL_COLOR_MODE_NATIVE, std::string("native")},
        {HAL_COLOR_MODE_SRGB, std::string("sRGB")},
        {HAL_COLOR_MODE_DISPLAY_P3, std::string("displayP3")},
    };
};
