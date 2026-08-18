import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "dialogs"
import "panes"
import "widgets"

ApplicationWindow {
    title: "MAKO Renderer Configuration"
    width: 900
    height: 550
    minimumWidth: 700
    minimumHeight: 400
    visible: true

    CenteredDialog {
        id: create_dialog
        name: "Create New Profile"
        onConfirm: backend.createProfile(create_name.text)

        TextField {
            id: create_name
            Layout.fillWidth: true
            placeholderText: "Choose a profile name"
            focus: true
        }
    }

    CenteredDialog {
        id: rename_dialog
        name: "Rename Profile"
        onConfirm: backend.renameProfile(rename_name.text)

        TextField {
            id: rename_name
            Layout.fillWidth: true
            placeholderText: "Choose a profile name"
            focus: true
        }
    }

    CenteredDialog {
        id: delete_dialog
        name: "Confirm Deletion"
        onConfirm: backend.deleteProfile()

        Label {
            Layout.fillWidth: true
            text: "Are you sure you want to delete the selected profile?"
            horizontalAlignment: Text.AlignHCenter
        }
    }

    LargeDialog {
        id: active_in_dialog
        name: "Active In"

        List {
            Layout.fillWidth: true
            Layout.fillHeight: true

            model: backend.active_in
            selected: backend.active_in_index
            onSelect: index => {
                backend.active_in_index = index;
                var idx = backend.active_in.index(index, 0);
                active_in_name.text = backend.active_in.data(idx);
            }
        }

        RowLayout {
            spacing: 8

            TextField {
                id: active_in_name
                Layout.fillWidth: true
                placeholderText: "Specify linux binary / exe file / process name"
                focus: true
            }
            Button {
                icon.name: "list-add"
                onClicked: backend.addActiveIn(active_in_name.text)
            }
            Button {
                icon.name: "list-remove"
                onClicked: backend.removeActiveIn()
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Pane {
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 250
            SplitView.maximumWidth: 300

            Label {
                text: "Profiles"
                Layout.fillWidth: true
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            List {
                model: backend.profiles
                selected: backend.profile_index
                onSelect: index => backend.profile_index = index
            }

            Button {
                Layout.fillWidth: true
                text: "Create New Profile"
                onClicked: {
                    create_name.text = "";
                    create_dialog.open();
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Rename Profile"
                onClicked: {
                    var idx = backend.profiles.index(backend.profile_index, 0);
                    rename_name.text = backend.profiles.data(idx);
                    rename_dialog.open();
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Delete Profile"
                onClicked: {
                    delete_dialog.open();
                }
            }
        }

        ScrollView {
            id: settings_scroll
            SplitView.fillWidth: true
            clip: true
            contentWidth: availableWidth
            leftPadding: 12
            rightPadding: 12
            topPadding: 12
            bottomPadding: 12
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: settings_scroll.availableWidth
                spacing: 12

                Group {
                    name: "Global Settings"

                    GroupEntry {
                        title: "Lossless.dll Path"
                        description: "Leave blank for automatic discovery, or choose the licensed DLL explicitly"

                        FileEdit {
                            Layout.fillWidth: true

                            title: "Select Lossless.dll"
                            filter: "Dynamic Link Library Files (*.dll)"

                            text: backend.dll
                            onUpdate: text => backend.dll = text
                        }
                    }

                    GroupEntry {
                        title: "Allow FP16"
                        description: "Improves performance on AMD; disable for older NVIDIA GPUs"

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.allow_fp16
                            onToggled: backend.allow_fp16 = checked
                        }
                    }
                }

                Group {
                    name: "Profile Settings"
                    enabled: backend.available

                    GroupEntry {
                        title: "Frame Generation"
                        description: "Stop or resume frame synthesis live while preserving the selected mode"

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.frame_generation_enabled
                            onToggled: backend.frame_generation_enabled = checked
                        }
                    }

                    GroupEntry {
                        title: "Base FPS Cap"
                        description: "Cap real application frames before frame generation; set to Off to disable"
                        enabled: !(backend.adaptive && backend.adaptive_auto_base_fps_cap)

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: 0
                            to: 1000
                            editable: true

                            value: backend.base_fps_cap
                            textFromValue: function (value) {
                                return value === 0 ? "Off" : value + " FPS";
                            }
                            valueFromText: function (text) {
                                var parsed = parseInt(text);
                                return isNaN(parsed) ? 0 : Math.max(0, Math.min(1000, parsed));
                            }
                            onValueModified: backend.base_fps_cap = value
                        }
                    }

                    GroupEntry {
                        title: "Active In"
                        description: "Specify which applications this profile is active in"

                        Button {
                            Layout.alignment: Qt.AlignRight

                            text: "Edit..."
                            onClicked: active_in_dialog.open()
                        }
                    }

                    GroupEntry {
                        title: "Adaptive Frame Generation"
                        description: "Vary generated frames to approach a target framerate (maximum 4x)"

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive
                            onToggled: backend.adaptive = checked
                        }
                    }

                    GroupEntry {
                        title: "Target FPS"
                        description: "Desired output; the multiplier limit may intentionally keep it below target"
                        enabled: backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: 10
                            to: 1000

                            value: backend.target_fps
                            onValueModified: backend.target_fps = value
                        }
                    }

                    GroupEntry {
                        title: "Adaptive FPS Cap (" + (backend.target_fps / 2) + " FPS)"
                        description: "Cap real FPS to half the target for a steadier 2x baseline"
                        enabled: backend.adaptive

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive_auto_base_fps_cap
                            onToggled: backend.adaptive_auto_base_fps_cap = checked
                        }
                    }

                    GroupEntry {
                        title: "Maximum Adaptive Multiplier"
                        description: "Limit interpolation to protect image quality when the real framerate falls"
                        enabled: backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: 2
                            to: 4

                            value: backend.adaptive_max_multiplier
                            textFromValue: function (value) {
                                return value + "x";
                            }
                            valueFromText: function (text) {
                                return parseInt(text);
                            }
                            onValueModified: backend.adaptive_max_multiplier = value
                        }
                    }

                    GroupEntry {
                        title: "Smooth Cadence"
                        description: "Prefer smoother constant interpolation at the cost of lower real-frame cadence and responsiveness"
                        enabled: backend.adaptive

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive_stable_cadence
                            onToggled: backend.adaptive_stable_cadence = checked
                        }
                    }

                    GroupEntry {
                        title: "Multiplier"
                        description: "Control the amount of generated frames"
                        enabled: !backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: 2
                            to: 100

                            value: backend.multiplier
                            onValueModified: backend.multiplier = value
                        }
                    }

                    GroupEntry {
                        title: "Flow Scale"
                        description: "Lower the internal motion estimation resolution"

                        FlowSlider {
                            Layout.fillWidth: true

                            from: 0.25
                            to: 1.00

                            value: backend.flow_scale
                            onUpdate: value => backend.flow_scale = value
                        }
                    }

                    GroupEntry {
                        title: "Performance Mode"
                        description: "Can improve performance at the cost of ghosting; start disabled and test per game"

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.performance_mode
                            onToggled: backend.performance_mode = checked
                        }
                    }

                    GroupEntry {
                        title: "Pacing Mode"
                        description: "Change how frames are presented to the display"

                        ComboBox {
                            Layout.fillWidth: true

                            model: ["None"]
                            currentIndex: backend.pacing_mode
                            onActivated: index => backend.pacing_mode = index
                        }
                    }

                    GroupEntry {
                        title: "GPU"
                        description: "Select which GPU to use for frame generation"

                        ComboBox {
                            Layout.fillWidth: true

                            model: backend.gpus
                            currentIndex: backend.gpu
                            onActivated: index => backend.gpu = index
                        }
                    }
                }

                Group {
                    name: "Decky Integration"

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Automatic game discovery, launch compatibility, Flatpak setup, and Renderer installation are managed in MAKO Decky."
                        color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                    }
                }
            }
        }
    }
}
