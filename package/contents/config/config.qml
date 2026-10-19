import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18n("角色")
        icon: "org.kde.plasma.live2d"
        source: "configGeneral.qml"
    }
    ConfigCategory {
        name: i18n("功能")
        icon: "preferences-desktop"
        source: "configFeatures.qml"
    }
    ConfigCategory {
        name: i18n("渲染")
        icon: "video-display"
        source: "configRendering.qml"
    }
}
