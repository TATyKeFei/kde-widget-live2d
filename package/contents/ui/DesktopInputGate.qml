import QtQuick
import QtQml

QtObject {
    id: root
    required property Item targetItem
    property bool active: false
    property bool editing: false

    // Plasma wraps each applet in its own input-handling container. Disable
    // only that wrapper, never the shared desktop window or applets layout.
    // Standalone hosts without a wrapper fall back to the applet itself.
    readonly property Item inputItem: {
        for (let item = targetItem.parent; item; item = item.parent) {
            if (item.applet !== undefined && item.applet === targetItem)
                return item;
        }
        return targetItem;
    }

    readonly property Binding inputBinding: Binding {
        target: root.inputItem
        property: "enabled"
        value: false
        when: root.active && !root.editing
        restoreMode: Binding.RestoreBindingOrValue
    }
    Component.onDestruction: inputBinding.when = false
}
