-- Editor UI state
-- Restructured to match standard LuaUIState format (data + methods)

return {
    -- Data section: reactive values that drive the UI template
    data = {
        activeView = "scene",
        fontSize = 18,
        focusedField = "",
        cursorPos = 0,

        layout = {
            leftWidth = 240,
            inspectorWidth = 320
        },

        resizing = {
            active = false,
            target = "",
            startX = 0,
            startLeftWidth = 240,
            startInspectorWidth = 320
        },

        scene = {
            path = "../res/scenes/EditorTest.yaml",
            currentPath = "",
            dirty = false,
            error = ""
        },
        entities = {},
        selectedEntityId = nil,
        selectedEntityName = "",
        inspector = {
            posX = "0",
            posY = "0",
            posZ = "0",
            rotX = "0",
            rotY = "0",
            rotZ = "0",
            scaleX = "1",
            scaleY = "1",
            scaleZ = "1"
        },
        -- Display values with cursor inserted (for focused field only)
        inspectorDisplay = "",

        -- Button press state (using @mousedown/@mouseup)
        pressedButton = "",

        -- Hover state (using @mouseenter/@mouseleave)
        hovered = {
            entityId = nil,        -- ID of hovered entity in scene tree
            menuItem = "",         -- which menu item is hovered
            inspectorField = ""    -- path of hovered inspector field
        },

        -- UI Editor state
        uiEditor = {
            currentTemplate = "",
            templates = {},
            htmlCode = "",
            cssCode = "",
            luaCode = "",
            previewHTML = "",
            isDirty = false,
            activeTab = "html",
            errorMessage = "",
            newTemplateName = ""
        }
    },

    -- Methods section: event handlers called from UI interactions
    -- Note: C++ code may add additional handlers (e.g., selectEntity) at runtime
    methods = {
        setActiveView = function(self, viewName)
            self.data.activeView = viewName
        end,

        toggleView = function(self)
            if self.data.activeView == "scene" then
                self.data.activeView = "ui"
            else
                self.data.activeView = "scene"
            end
        end,

        noop = function(self)
            -- Intentionally empty (used to consume clicks on splitters/overlays)
        end,

        increaseFont = function(self)
            self.data.fontSize = math.min(self.data.fontSize + 1, 24)
        end,

        decreaseFont = function(self)
            self.data.fontSize = math.max(self.data.fontSize - 1, 10)
        end,

        beginResizeLeft = function(self, event)
            self.data.resizing.active = true
            self.data.resizing.target = "left"
            self.data.resizing.startX = event.x
            self.data.resizing.startLeftWidth = self.data.layout.leftWidth
        end,

        beginResizeInspector = function(self, event)
            self.data.resizing.active = true
            self.data.resizing.target = "inspector"
            self.data.resizing.startX = event.x
            self.data.resizing.startInspectorWidth = self.data.layout.inspectorWidth
        end,

        onMouseMove = function(self, event)
            if not self.data.resizing.active then
                return
            end

            local dx = event.x - self.data.resizing.startX

            if self.data.resizing.target == "left" then
                local newWidth = self.data.resizing.startLeftWidth + dx
                self.data.layout.leftWidth = math.max(160, math.min(newWidth, 500))
            elseif self.data.resizing.target == "inspector" then
                local newWidth = self.data.resizing.startInspectorWidth - dx
                self.data.layout.inspectorWidth = math.max(200, math.min(newWidth, 600))
            end
        end,

        endResize = function(self)
            self.data.resizing.active = false
            self.data.resizing.target = ""
        end,

        switchTab = function(self, tabName)
            self.data.uiEditor.activeTab = tabName
        end,

        -- Button press handlers (dogfooding @mousedown/@mouseup)
        onButtonPress = function(self, buttonName)
            self.data.pressedButton = buttonName
        end,

        onButtonRelease = function(self)
            self.data.pressedButton = ""
        end,

        -- Hover handlers (dogfooding @mouseenter/@mouseleave)
        onEntityHover = function(self, entityId)
            self.data.hovered.entityId = entityId
        end,

        onEntityLeave = function(self)
            self.data.hovered.entityId = nil
        end,

        onMenuHover = function(self, menuName)
            self.data.hovered.menuItem = menuName
        end,

        onMenuLeave = function(self)
            self.data.hovered.menuItem = ""
        end,

        onFieldHover = function(self, fieldPath)
            self.data.hovered.inspectorField = fieldPath
        end,

        onFieldLeave = function(self)
            self.data.hovered.inspectorField = ""
        end,

        -- Reserved for inspector hooks (implemented in C++ for now)
    }
}
