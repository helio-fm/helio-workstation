/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

class AudioPlugin;
class AudioPluginEditor;

#include "TreeItem.h"

class AudioPluginTreeItem final : public TreeItem
{
public:

    AudioPluginTreeItem(uint32 pluginID, const String &name);

    Colour getColour() const noexcept override;
    Image getIcon() const noexcept override;
    uint32 getNodeId() const noexcept;

    bool hasMenu() const noexcept override;
    ScopedPointer<Component> createMenu() override;

    void showPage() override;
    
private:

    ScopedPointer<Component> audioPluginEditor;
    const uint32 filterID;

};
