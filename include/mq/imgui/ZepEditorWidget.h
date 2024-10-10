/*
 * MacroQuest: The extension platform for EverQuest
 * Copyright (C) 2002-present MacroQuest Authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include <string>

namespace mq {
	struct ImGuiZepEditor;
}

namespace mq::imgui {

	// Interface to a ImGuiZepInputBox, suitable for exposure to plugins.

	class ZepEditorWidget
	{
	public:
		static inline const MQColor DEFAULT_COLOR = MQColor(240, 240, 240, 255);

		MQLIB_OBJECT static  std::shared_ptr<ZepEditorWidget> Create(std::string_view id);
		virtual ~ZepEditorWidget() {};

		virtual void Render(const ImVec2& displaySize = ImVec2()) = 0;

		virtual void Clear() = 0;
		virtual void AppendText(std::string_view text, bool appendNewLine = false) = 0;

		virtual bool IsCursorAtEnd() const = 0;

		virtual void SetSyntax(const char* syntaxName) const = 0;

		virtual int GetFontSize() const = 0;
		virtual void SetFontSize(int size) = 0;

		virtual int GetWindowFlags() const = 0;
		virtual void SetWindowFlags(int flags) const = 0;

		virtual void GetText(std::string& outBuffer) const = 0;
		virtual size_t GetTextLength() const = 0;
	};

} // namespace mq::imgui
