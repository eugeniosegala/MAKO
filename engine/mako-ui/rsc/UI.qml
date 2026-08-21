import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "dialogs"
import "panes"
import "widgets"
import "i18n/translations.js" as Translations

ApplicationWindow {
    property int lang: 0
    property var t: Translations.strings[lang]

    title: t.makoRendererConfig
    width: 900
    height: 550
    minimumWidth: 700
    minimumHeight: 400
    visible: true

    CenteredDialog {
        id: create_dialog
        name: t.createNewProfile
        onConfirm: backend.createProfile(create_name.text)

        TextField {
            id: create_name
            Layout.fillWidth: true
            placeholderText: t.chooseProfileName
            focus: true
        }
    }

    CenteredDialog {
        id: rename_dialog
        name: t.renameProfile
        onConfirm: backend.renameProfile(rename_name.text)

        TextField {
            id: rename_name
            Layout.fillWidth: true
            placeholderText: t.chooseProfileName
            focus: true
        }
    }

    CenteredDialog {
        id: delete_dialog
        name: t.confirmDeletion
        onConfirm: backend.deleteProfile()

        Label {
            Layout.fillWidth: true
            text: t.confirmDeleteMsg
            horizontalAlignment: Text.AlignHCenter
        }
    }

    LargeDialog {
        id: active_in_dialog
        name: t.activeIn

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
                placeholderText: t.activeInPlaceholder
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
                text: t.profiles
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
                text: t.createNewProfile
                onClicked: {
                    create_name.text = "";
                    create_dialog.open();
                }
            }
            Button {
                Layout.fillWidth: true
                text: t.renameProfile
                onClicked: {
                    var idx = backend.profiles.index(backend.profile_index, 0);
                    rename_name.text = backend.profiles.data(idx);
                    rename_dialog.open();
                }
            }
            Button {
                Layout.fillWidth: true
                text: t.deleteProfile
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
                    name: t.globalSettings

                    GroupEntry {
                        title: t.losslessDllPath
                        description: t.losslessDllDesc

                        FileEdit {
                            Layout.fillWidth: true

                            title: t.selectLosslessDll
                            filter: t.dllFilter

                            text: backend.dll
                            onUpdate: text => backend.dll = text
                        }
                    }

                    GroupEntry {
                        title: t.allowFp16
                        description: t.allowFp16Desc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.allow_fp16
                            onToggled: backend.allow_fp16 = checked
                        }
                    }
                }

                Group {
                    name: t.profileSettings
                    enabled: backend.available

                    GroupEntry {
                        title: t.frameGeneration
                        description: t.frameGenerationDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.frame_generation_enabled
                            onToggled: backend.frame_generation_enabled = checked
                        }
                    }

                    GroupEntry {
                        title: t.baseFpsCap
                        description: t.baseFpsCapDesc
                        enabled: !(backend.adaptive && backend.adaptive_auto_base_fps_cap)

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: 0
                            to: 1000
                            editable: true

                            value: backend.base_fps_cap
                            textFromValue: function (value) {
                                return value === 0 ? t.off : value + t.fps;
                            }
                            valueFromText: function (text) {
                                var parsed = parseInt(text);
                                return isNaN(parsed) ? 0 : Math.max(0, Math.min(1000, parsed));
                            }
                            onValueModified: backend.base_fps_cap = value
                        }
                    }

                    GroupEntry {
                        title: t.activeIn
                        description: t.activeInDesc

                        Button {
                            Layout.alignment: Qt.AlignRight

                            text: t.editEllipsis
                            onClicked: active_in_dialog.open()
                        }
                    }

                    GroupEntry {
                        title: t.adaptiveFrameGen
                        description: t.adaptiveFrameGenDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive
                            onToggled: backend.adaptive = checked
                        }
                    }

                    GroupEntry {
                        title: t.targetFps
                        description: t.targetFpsDesc
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
                        title: t.adaptiveFpsCapPrefix + (backend.target_fps / 2) + t.adaptiveFpsCapSuffix
                        description: t.adaptiveFpsCapDesc
                        enabled: backend.adaptive

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive_auto_base_fps_cap
                            onToggled: backend.adaptive_auto_base_fps_cap = checked
                        }
                    }

                    GroupEntry {
                        title: t.maxAdaptiveMultiplier
                        description: t.maxAdaptiveMultiplierDesc
                        enabled: backend.adaptive

                        SpinBox {
                            Layout.alignment: Qt.AlignRight

                            from: 2
                            to: 4

                            value: backend.adaptive_max_multiplier
                            textFromValue: function (value) {
                                return value + t.multiplierX;
                            }
                            valueFromText: function (text) {
                                return parseInt(text);
                            }
                            onValueModified: backend.adaptive_max_multiplier = value
                        }
                    }

                    GroupEntry {
                        title: t.smoothCadence
                        description: t.smoothCadenceDesc
                        enabled: backend.adaptive

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.adaptive_stable_cadence
                            onToggled: backend.adaptive_stable_cadence = checked
                        }
                    }

                    GroupEntry {
                        title: t.multiplier
                        description: t.multiplierDesc
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
                        title: t.flowScale
                        description: t.flowScaleDesc

                        FlowSlider {
                            Layout.fillWidth: true

                            from: 0.25
                            to: 1.00

                            value: backend.flow_scale
                            onUpdate: value => backend.flow_scale = value
                        }
                    }

                    GroupEntry {
                        title: t.performanceMode
                        description: t.performanceModeDesc

                        CheckBox {
                            Layout.alignment: Qt.AlignRight

                            checked: backend.performance_mode
                            onToggled: backend.performance_mode = checked
                        }
                    }

                    GroupEntry {
                        title: t.pacingMode
                        description: t.pacingModeDesc

                        ComboBox {
                            Layout.fillWidth: true

                            model: [t.none]
                            currentIndex: backend.pacing_mode
                            onActivated: index => backend.pacing_mode = index
                        }
                    }

                    GroupEntry {
                        title: t.gpu
                        description: t.gpuDesc

                        ComboBox {
                            Layout.fillWidth: true

                            model: backend.gpus
                            currentIndex: backend.gpu
                            onActivated: index => backend.gpu = index
                        }
                    }
                }

                Group {
                    name: t.deckyIntegration

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: t.deckyDesc
                        color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                    }
                }

                Group {
                    name: t.language

                    ComboBox {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignRight

                        model: Translations.languageNames
                        currentIndex: lang
                        onActivated: index => lang = index
                    }
                }
            }
        }
    }
}
