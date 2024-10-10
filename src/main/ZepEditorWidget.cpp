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

#include "pch.h"

#include "MQ2DeveloperTools.h"
#include "MQ2ImGuiTools.h"
#include "MQ2Utilities.h"
#include "ImGuiZepEditor.h"
#include "ImGuiManager.h"

#include "imgui/ImGuiTreePanelWindow.h"
#include "mq/imgui/ZepEditorWidget.h"

#include <imgui/imgui_internal.h>

#include "zep.h"
#include <optional>
#include "sqlite3.h"

namespace mq {

#pragma region Zep Integration

//----------------------------------------------------------------------------

class ZepMode_ImGuiInputBox : public Zep::ZepMode_Standard
{
public:
	using ZepMode_Standard::ZepMode_Standard;

	virtual Zep::CursorType GetCursorType() const override { return Zep::CursorType::Normal; }

	static const char* StaticName()
	{
		return "ZepEditor";
	}

	virtual const char* Name() const override
	{
		return StaticName();
	}
};

//----------------------------------------------------------------------------

// This is the imgui container for the Zep component.
struct ImGuiZepEditorWidget : public mq::imgui::ZepEditorWidget, public mq::imgui::ImGuiZepEditor
{
	Zep::ZepBuffer* m_buffer = nullptr;
	Zep::ZepWindow* m_window = nullptr;
	int m_fontSize = 13;
	std::string m_id;

	ImGuiZepEditorWidget(std::string_view id)
		: m_id(std::string(id))
	{
		SetFont(Zep::ZepTextType::Text, mq::imgui::ConsoleFont, m_fontSize);

		GetEditor().RegisterGlobalMode(std::make_shared<ZepMode_ImGuiInputBox>(GetEditor()));
		GetEditor().SetGlobalMode(ZepMode_ImGuiInputBox::StaticName());

		m_window = GetEditor().GetActiveTabWindow()->GetActiveWindow();

		GetEditor().GetConfig().style = Zep::EditorStyle::Normal;
		m_window->SetWindowFlags(Zep::WindowFlags::WrapText);

		m_buffer = GetEditor().InitWithText("InputBox", "");
		m_buffer->SetFileFlags(Zep::FileFlags::CrudeUtf8Vaidate);
	}

	void Clear() override
	{
		m_buffer->Clear();
	}

	void SetSyntaxProvider(const char* syntaxName) const
	{
		GetEditor().SetBufferSyntax(*m_buffer, syntaxName);
	}

	Zep::ZepWindow* GetWindow() const { return m_window; }
	Zep::ZepBuffer* GetBuffer() const { return m_buffer; }
	
	void GetText(std::string& outBuffer) const override
	{
		outBuffer.reserve(GetTextLength());
		auto & workingBuffer = m_buffer->GetWorkingBuffer();
		std::copy(workingBuffer.begin(), workingBuffer.end(), std::back_inserter(outBuffer));
		// copy 1 less character to avoid adding the null terminator into the std::string size.
		outBuffer.pop_back();
	}

	size_t GetTextLength() const
	{
		// buffer size includes the null terminator the text length should not.
		return m_buffer->GetWorkingBuffer().size() - 1;
	}

	Zep::GlyphIterator InsertText(Zep::GlyphIterator position, std::string_view text)
	{
		Zep::ChangeRecord changeRecord;
		m_buffer->Insert(position, text, changeRecord);

		return position.Move(static_cast<long>(text.length()));
	}

	void AppendText(std::string_view text,  bool appendNewLine /* = false */) override
	{
		Zep::GlyphIterator cursor = m_window->GetBufferCursor();
		bool cursorAtEnd = m_window->IsAtBottom();

		std::string_view lineView = text;

		std::vector<ImU32> colorStack;

		InsertText(m_buffer->End(), lineView);

		if (appendNewLine)
			InsertText(m_buffer->End(), "\n");
	}

	bool IsCursorAtEnd() const override
	{
		return m_window->IsAtBottom();
	}

	void Render(const ImVec2& displaySize = ImVec2()) override
	{
		ImGuiZepEditor::Render(m_id.c_str(), displaySize);
	}

	void Notify(std::shared_ptr<Zep::ZepMessage> message) override
	{
		ImGuiZepEditor::Notify(message);
	}

	void SetSyntax(const char* syntaxName) const override
	{
		SetSyntaxProvider(syntaxName);
	}
	
	int GetFontSize() const override { return m_fontSize; }
	void SetFontSize(int size) override
	{
		m_fontSize = size;
		SetFont(Zep::ZepTextType::Text, mq::imgui::ConsoleFont, m_fontSize);
	}
	
	int GetWindowFlags() const override { return m_window->GetWindowFlags(); }
	void SetWindowFlags(int flags) const override
	{
		m_window->SetWindowFlags(flags);
	}
};

#pragma endregion

namespace imgui
{
	std::shared_ptr<mq::imgui::ZepEditorWidget> ZepEditorWidget::Create(std::string_view id)
	{
		return std::make_shared<ImGuiZepEditorWidget>(id);
	}
}

} // namespace mq
