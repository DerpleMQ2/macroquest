/*
 * MacroQuest2: The extension platform for EverQuest
 * Copyright (C) 2002-2019 MacroQuest Authors
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
#include "MQ2Main.h"

#include "imgui/ImGuiTreePanelWindow.h"
#include "imgui/ImGuiColorTextEdit.h"
#include <imgui/imgui_internal.h>

#include <fmt/format.h>

using namespace std::chrono_literals;

namespace mq {

static bool gbShowDemoWindow = false;
static bool gbShowSettingsWindow = false;
static bool gbShowDebugWindow = false;
static bool gbShowConsoleWindow = true;
static int gRenderCallbacksId = 0;

imgui::ImGuiTreePanelWindow gSettingsWindow("MacroQuest Settings");
imgui::ImGuiTreePanelWindow gDebugWindow("MacroQuest Debug Tools");

static void InitializeMQ2ImGuiAPI();
static void ShutdownMQ2ImGuiAPI();
static void PulseMQ2ImGuiAPI();
static void UpdateOverlayUI();
static DWORD WriteChatColorImGuiAPI(const char* line, DWORD color, DWORD filter);

static MQModule gImGuiModule = {
	"ImGuiAPI",                   // Name
	false,                        // CanUnload
	InitializeMQ2ImGuiAPI,        // Initialize
	ShutdownMQ2ImGuiAPI,          // Shutdown
	PulseMQ2ImGuiAPI,             // Pulse
	nullptr,                      // SetGameState
	UpdateOverlayUI,              // UpdateImGui
	nullptr,                      // Zoned
	WriteChatColorImGuiAPI,       // WriteChatColor
};
MQModule* GetImGuiAPIModule() { return &gImGuiModule; }

//----------------------------------------------------------------------------

static const ImU32 s_defaultColor = IM_COL32(0xf0, 0xf0, 0xf0, 255);
static ImGuiID s_dockspaceId = 0;
static bool s_dockspaceVisible = true;
static bool s_resetDockspace = false;

const mq::imgui::TextEditor::Palette& GetColorPalette()
{
	const static mq::imgui::TextEditor::Palette p = { {
			0xffffffff, // Default
			0xffffffff, // Keyword
			0xffffffff, // Number
			0xffffffff, // String
			0xffffffff, // Char literal
			0xffffffff, // Punctuation
			0xffffffff, // Preprocessor
			0xffffffff, // Identifier
			0xffffffff, // Known identifier
			0xffffffff, // Preproc identifier
			0xffffffff, // Comment (single line)
			0xffffffff, // Comment (multi line)
			0xff101010, // Background
			0xffe0e0e0, // Cursor
			0x80a06020, // Selection
			0x800020ff, // ErrorMarker
			0x40f08000, // Breakpoint
			0xff707000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40a0a0a0, // Current line edge
		} };
	return p;
}

//----------------------------------------------------------------------------

static void Strtrim(char* str)
{
	char* str_end = str + strlen(str);
	while (str_end > str && str_end[-1] == ' ')
		str_end--;
	*str_end = 0;
}

unsigned int str_to_hex(char const* p, char const* e) noexcept
{
	unsigned int result = 0;
	while (p != e)
	{
		result *= 16;
		if ('0' <= *p && *p <= '9') { result += *p - '0'; p++; continue; }
		if ('A' <= *p && *p <= 'F') { result += *p + 10 - 'A'; p++; continue; }
		if ('a' <= *p && *p <= 'f') { result += *p + 10 - 'a'; p++; continue; }
		return -1;
	}

	return result;
}

ImU32 str_to_col(std::string_view str)
{
	if (str.length() != 7 || str[0] != '#') { return 0; }

	auto r = str_to_hex(str.data() + 1, str.data() + 3);
	auto g = str_to_hex(str.data() + 3, str.data() + 5);
	auto b = str_to_hex(str.data() + 5, str.data() + 7);

	return IM_COL32(r, g, b, 255);
}

class ImGuiConsole
{
public:
	char                     m_inputBuffer[2048];
	ImVector<const char*>    m_commands;
	std::vector<std::string> m_history;
	int                      m_historyPos;          // -1: new line, 0..History.Size-1 browsing history.
	bool                     m_autoScroll;
	bool                     m_scrollToBottom;
	mq::imgui::TextEditor    m_editor;

	ImGuiConsole()
		: m_historyPos(-1)
		, m_autoScroll(true)
		, m_scrollToBottom(true)
	{
		ZeroMemory(m_inputBuffer, lengthof(m_inputBuffer));

		m_editor.SetPalette(GetColorPalette());
		m_editor.SetReadOnly(true);
		m_editor.SetRenderCursor(false);
		m_editor.SetShowWhitespace(false);
		//m_editor.SetRenderLineNumbers(false);
		//m_editor.SetImGuiChildIgnored(true);
	}

	~ImGuiConsole()
	{
		ClearLog();
	}

	void ClearLog()
	{
		m_editor.SetText("");
	}

	template <typename... Args>
	void AddLog(ImU32 color, std::string_view fmt, const Args&... args)
	{
		fmt::basic_memory_buffer<char> buf;
		fmt::format_to(buf, fmt, args...);

		m_editor.MoveBottom();
		m_editor.MoveEnd();
		m_editor.InsertText(std::string_view(buf.data(), buf.size()), color);
	}

	template <typename... Args>
	void AddLog(std::string_view fmt, const Args&... args)
	{
		AddLog(s_defaultColor, std::move(fmt), args...);
	}

	static std::pair<std::string_view, ImU32> ParseColor(std::string_view line, ImU32 color)
	{
		size_t length = line.length();
		const char* pos = line.data();
		const char* end = line.data() + length;

		// skip over the \a
		if (*(pos++) != '\a')
			return { {}, color };

		if (pos == end) return { {}, color };

		bool dark = false;

		// clear
		if (*pos == 'x')
		{
			pos++;
			return { std::string_view{ pos, (size_t)(end - pos) }, s_defaultColor };
		}

		// custom color
		if (*pos == '#')
		{
			// we need 7 to do anything (6 for hex code and 1 for #)
			if (end - pos < 7) return { {}, color };
			std::string_view colorCode{ pos, 7 };

			// convert hex to color
			color = str_to_col(colorCode);

			pos += 7;
			return { std::string_view{ pos, (size_t)(end - pos) }, color };
		}

		// darken
		if (*pos == '-')
		{
			dark = true;
			pos++;

			if (pos == end) return { {}, color };
		}

		switch (*pos)
		{
		case 'y': // yellow (green/red)
			if (dark)
				color = 0xff009999;
			else
				color = 0xff00ffff;
			break;
		case 'o': // orange (green/red)
			if (dark)
				color = 0xff006699;
			else
				color = 0xff0099ff;
			break;
		case 'g': // green   (green)
			if (dark)
				color = 0xff009900;
			else
				color = 0xff00ff00;
			break;
		case 'u': // blue   (blue)
			if (dark)
				color = 0xff990000;
			else
				color = 0xffff0000;
			break;
		case 'r': // red     (red)
			if (dark)
				color = 0xff000099;
			else
				color = 0xff0000ff;
			break;
		case 't': // teal (blue/green)
			if (dark)
				color = 0xff999900;
			else
				color = 0xffffff00;
			break;
		case 'b': // black   (none)
			color = 0xff000000;
			break;
		case 'm': // magenta (blue/red)
			if (dark)
				color = 0xff990099;
			else
				color = 0xffff00ff;
			break;
		case 'p': // purple (blue/red)
			if (dark)
				color = 0xff990066;
			else
				color = 0xffff0099;
			break;
		case 'w': // white   (all)
			if (dark)
				color = 0xff999999;
			else
				color = 0xffffffff;
			break;
		}
		pos++;

		return { { pos, (size_t)(end - pos) }, color };
	}

	void AddWriteChatColorLog(const char* line, ImU32 defaultColor = s_defaultColor, bool newline = false)
	{
		m_editor.MoveBottom();
		m_editor.MoveEnd();

		std::string_view lineView{ line };
		ImU32 currentColor = defaultColor;

		while (!lineView.empty())
		{
			auto colorPos = lineView.find("\a");

			// this is everything before the color code.
			auto beforeColor = lineView.substr(0, colorPos);
			if (!beforeColor.empty())
			{
				// no color codes, write out with current color
				m_editor.InsertText(beforeColor, currentColor);
			}

			// did we find a color?
			if (colorPos == std::string_view::npos)
				break;

			lineView = lineView.substr(colorPos);

			auto& [nextSegment, nextColor] = ParseColor(lineView, defaultColor);
			// Parse the color and get the next segment. We pass in the
			// default color to handle \ax properly

			if (nextSegment.empty())
				break;

			currentColor = nextColor;
			lineView = nextSegment;
		}

		if (newline)
			m_editor.InsertText("\n");
	}

	void Draw(bool* pOpen)
	{
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;

		if (!ImGui::Begin("MacroQuest Console", pOpen, windowFlags))
		{
			ImGui::End();
			return;
		}

		// As a specific feature guaranteed by the library, after calling Begin() the last Item
		// represent the title bar. So e.g. IsItemHovered() will return true when hovering the title bar.
		// Here we create a context menu only available from the title bar.
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Options"))
			{
				ImGui::MenuItem("Auto-scroll", nullptr, &m_autoScroll);

				ImGui::Separator();

				if (ImGui::MenuItem("Close Console"))
					*pOpen = false;
				if (ImGui::MenuItem("Reset Position"))
					s_resetDockspace = true;

				ImGui::Separator();

				if (ImGui::MenuItem("Clear Console"))
					ClearLog();

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing(); // 1 separator, 1 input text
		ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar); // Leave room for 1 separator + 1 InputText
		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::Selectable("Clear")) ClearLog();
			ImGui::EndPopup();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
		ImGui::PushAllowKeyboardFocus(false);
		m_editor.Render("TextEditor");
		ImGui::PopAllowKeyboardFocus();
		if (m_scrollToBottom || (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
			ImGui::SetScrollHereY(1.0f);
		m_scrollToBottom = false;
		ImGui::PopStyleVar();
		ImGui::EndChild();
		ImGui::Separator();

		// Command-line
		bool reclaim_focus = false;

		int textFlags = ImGuiInputTextFlags_EnterReturnsTrue
			| ImGuiInputTextFlags_CallbackCompletion
			| ImGuiInputTextFlags_CallbackHistory;

		ImGui::PushItemWidth(-1);
		bool bTextEdit = ImGui::InputText("##Input", m_inputBuffer, IM_ARRAYSIZE(m_inputBuffer), textFlags,
			[](ImGuiInputTextCallbackData* data)
			{ return static_cast<ImGuiConsole*>(data->UserData)->TextEditCallback(data); }, this);

		ImGui::PopItemWidth();
		if (bTextEdit)
		{
			char* s = m_inputBuffer;
			Strtrim(s);
			if (s[0])
				ExecCommand(s);
			strcpy_s(s, MAX_STRING, "");
			reclaim_focus = true;
		}

		// Auto-focus on window apparition
		ImGui::SetItemDefaultFocus();
		if (reclaim_focus)
			ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

		ImGui::End();
	}

	void ExecCommand(const char* commandLine)
	{
		AddLog(IM_COL32(0x80, 0x80, 0x80, 255), "> {0}\n", commandLine);

		// Inhsert into history. First find match and delete it so i can be pushed to the back. This isn't
		// trying to be smart or optimal.
		m_historyPos = -1;

		for (int i = (int)m_history.size() - 1; i >= 0; --i)
		{
			if (ci_equals(m_history[i], commandLine))
			{
				m_history.erase(m_history.begin() + i);
				break;
			}
		}
		m_history.emplace_back(commandLine);

		// Process command
		if (ci_equals(commandLine, "clear"))
		{
			ClearLog();
		}
		else if (ci_equals(commandLine, "help"))
		{
			AddLog("Commands:\n");

			for (int i = 0; i < m_commands.Size; i++)
				AddLog("- {0}\n", m_commands[i]);
		}
		else if (ci_equals(commandLine, "history"))
		{
			int first = m_history.size() - 10;

			for (size_t i = first > 0 ? first : 0; i < m_history.size(); i++)
				AddLog("{0:3d}: {1}\n", i, m_history[i].c_str());
		}
		else if (strlen(commandLine) > 1 && commandLine[0] == '/')
		{
			mq::EzCommand(commandLine);
		}
		else
		{
			AddLog(IM_COL32(255, 0, 0, 255), "Unknown command: '{0}'\n", commandLine);
		}

		// On command input, we scroll to bottom even if AutoScroll == false
		m_scrollToBottom = true;
	}

	int TextEditCallback(ImGuiInputTextCallbackData* data)
	{
		switch (data->EventFlag)
		{
		case ImGuiInputTextFlags_CallbackCompletion:
			{
				// Example of TEXT COMPLETION

				// Locate beginning of current word
				const char* word_end = data->Buf + data->CursorPos;
				const char* word_start = word_end;
				while (word_start > data->Buf)
				{
					const char c = word_start[-1];
					if (c == ' ' || c == '\t' || c == ',' || c == ';')
						break;

					word_start--;
				}

				// Build a list of candidates
				ImVector<const char*> candidates;
				std::string_view word{ word_start, (size_t)(word_end - word_start) };

				for (int i = 0; i < m_commands.Size; i++)
				{
					if (ci_starts_with(m_commands[i], word))
						candidates.push_back(m_commands[i]);
				}

				if (candidates.Size == 0)
				{
					// No match
					AddLog("No match for \"{0}\"!\n", word);
				}
				else if (candidates.size() == 1)
				{
					// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing
					data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
					data->InsertChars(data->CursorPos, candidates[0]);
					data->InsertChars(data->CursorPos, " ");
				}
				else
				{
					// Multiple matches. Complete as much as we can, so inputing "C" will complete to "CL" and display "CLEAR" and "CLASSIFY"
					int match_len = (int)(word_end - word_start);
					for (;;)
					{
						int c = 0;
						bool all_candidates_matches = true;
						for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
							if (i == 0)
								c = toupper(candidates[i][match_len]);
							else if (c == 0 || c != toupper(candidates[i][match_len]))
								all_candidates_matches = false;
						if (!all_candidates_matches)
							break;
						match_len++;
					}

					if (match_len > 0)
					{
						data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
						data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
					}

					// List matches
					AddLog("Possible matches:\n");
					for (int i = 0; i < candidates.Size; i++)
						AddLog("- {0}\n", candidates[i]);
				}

				break;
			}

		case ImGuiInputTextFlags_CallbackHistory:
			{
				// Example of HISTORY
				const int prev_history_pos = m_historyPos;
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					if (m_historyPos == -1)
						m_historyPos = m_history.size() - 1;
					else if (m_historyPos > 0)
						m_historyPos--;
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					if (m_historyPos != -1)
						if (++m_historyPos >= (int)m_history.size())
							m_historyPos = -1;
				}

				// A better implementation would preserve the data on the current input line along with cursor position.
				if (prev_history_pos != m_historyPos)
				{
					const char* history_str = (m_historyPos >= 0) ? m_history[m_historyPos].c_str() : "";
					data->DeleteChars(0, data->BufTextLen);
					data->InsertChars(0, history_str);
				}
			}
		}
		return 0;
	}
};

ImGuiConsole gImGuiConsole;

//----------------------------------------------------------------------------

void DrawDockSpace()
{
	// when using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
	// and handle the pass-thru hole, so we ask Begin() to not render a background.
	ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None
		| ImGuiDockNodeFlags_PassthruCentralNode
		| ImGuiDockNodeFlags_NoDockingInCentralNode;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each other.
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoBackground;

	if (!s_dockspaceVisible)
		dockspaceFlags |= ImGuiDockNodeFlags_KeepAliveOnly;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

	// Important: note that we proceed even if Begin() returns false (a.k.a. window is collapsed).
	// This is because we want to keep our DockSpace() active. If DockSpace() is inactive,
	// all active windows docked to it will lose their parent and become undocked.

	// We cannot preserve the docking relationship between an active window and an inactive docking,
	// otherwise any change of dockspace/setings would leat to windows being stuck in limbo and never
	// being visible.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	bool display = ImGui::Begin("Overlay DockSpace Window", &s_dockspaceVisible, windowFlags);
	ImGui::PopStyleVar(3);

	// DockSpace
	ImGuiIO& op = ImGui::GetIO();
	s_dockspaceId = ImGui::GetID("Overlay DockSpace");

	ImGui::DockSpace(s_dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

	if (ImGui::DockBuilderGetNode(s_dockspaceId) == nullptr || s_resetDockspace)
	{
		s_resetDockspace = false;

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Reset layout
		ImGui::DockBuilderRemoveNode(s_dockspaceId);
		ImGui::DockBuilderAddNode(s_dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(s_dockspaceId, viewport->Size);

		// This variable will track the document node, however we are not using it
		// here as we aren't docking anything into it.
		ImGuiID dock_main_id = s_dockspaceId;

		ImGuiID dock_id_console = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.25f, nullptr, &dock_main_id);
		ImGui::DockBuilderDockWindow("MacroQuest Console", dock_id_console);

		ImGui::DockBuilderFinish(s_dockspaceId);
	}

	ImGui::End(); // end DockSpace()
}

void AddSettingsPanel(const char* name, fPanelDrawFunction drawFunction)
{
	gSettingsWindow.AddPanel(name, drawFunction);
}

void RemoveSettingsPanel(const char* name)
{
	gSettingsWindow.RemovePanel(name);
}

void AddDebugPanel(const char* name, fPanelDrawFunction drawFunction)
{
	gDebugWindow.AddPanel(name, drawFunction);
}

void RemoveDebugPanel(const char* name)
{
	gDebugWindow.RemovePanel(name);
}

static void DoToggleImGuiOverlay(const char* name, bool down)
{
	if (down)
	{
		SetOverlayEnabled(!IsOverlayEnabled());
	}
}

static void MakeColorGradient(float frequency1, float frequency2, float frequency3,
	float phase1, float phase2, float phase3,
	float center = 128, float width = 127, int length = 50)
{
	for (int i = 1; i < length + 1; ++i)
	{
		ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(
			(sin(frequency1 * i + phase1) * width + center) / 255,
			(sin(frequency2 * i + phase2) * width + center) / 255,
			(sin(frequency3 * i + phase3) * width + center) / 255, 1.0));

		std::string test = fmt::format("\a#{:06x}|", (color & 0xffffff));
		gImGuiConsole.AddWriteChatColorLog(test.c_str());

		if (i % 50 == 0)
			gImGuiConsole.AddLog("\n");
	}
}

void UpdateOverlayUI()
{
	// Initialize dockspace first so other windows can utilize it.+
	DrawDockSpace();

	if (ImGui::Begin("Debug"))
	{
		if (ImGui::Button("Toggle"))
			s_dockspaceVisible = !s_dockspaceVisible;
		if (ImGui::Button("Reset"))
			s_resetDockspace = true;
		if (ImGui::Button("Color Test"))
		{
			gImGuiConsole.AddWriteChatColorLog("\ayYELLOW    \a-yDARK YELLOW\n");
			gImGuiConsole.AddWriteChatColorLog("\aoORANGE    \a-oDARK ORANGE\n");
			gImGuiConsole.AddWriteChatColorLog("\agGREEN     \a-gDARK GREEN\n");
			gImGuiConsole.AddWriteChatColorLog("\auBLUE      \a-uDARK BLUE\n");
			gImGuiConsole.AddWriteChatColorLog("\arRED       \a-rDARK RED\n");
			gImGuiConsole.AddWriteChatColorLog("\atTEAL      \a-tDARK TEAL\n");
			gImGuiConsole.AddWriteChatColorLog("\abBLACK\n");
			gImGuiConsole.AddWriteChatColorLog("\amMAGENTA   \a-mDARK MAGENTA\n");
			gImGuiConsole.AddWriteChatColorLog("\apPURPLE    \a-pDARK PURPLE\n");
			gImGuiConsole.AddWriteChatColorLog("\awWHITE     \a-wGREY\n");

			MakeColorGradient(.3f, .3f, .3f, 0, 2, 4, 200, 50, 500);
		}
	}
	ImGui::End();

	if (s_dockspaceVisible)
	{
		gImGuiConsole.Draw(&s_dockspaceVisible);
	}

	if (gbShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&gbShowDemoWindow);
	}

	if (gbShowSettingsWindow)
	{
		gSettingsWindow.Draw(&gbShowSettingsWindow);
	}

	if (gbShowDebugWindow)
	{
		gDebugWindow.Draw(&gbShowDebugWindow);
	}
}

static ImU32 userColors[] = {
	IM_COL32(255, 255, 255, 255), //  1
	IM_COL32(190, 40,  190, 255), //  2
	IM_COL32(0,   255, 255, 255), //  3
	IM_COL32(40,  240, 40,  255), //  4
	IM_COL32(0,   128, 0,   255), //  5
	IM_COL32(0,   128, 0,   255), //  6
	IM_COL32(255, 0,   0,   255), //  7
	IM_COL32(90,  90,  255, 255), //  8
	IM_COL32(90,  90,  255, 255), //  9
	IM_COL32(255, 255, 255, 255), // 10
	IM_COL32(255, 0,   0,   255), // 11
	IM_COL32(255, 255, 255, 255), // 12
	IM_COL32(255, 255, 255, 255), // 13
	IM_COL32(255, 255, 0,   255), // 14
	IM_COL32(90,  90,  255, 255), // 15
	IM_COL32(255, 255, 255, 255), // 16
	IM_COL32(255, 255, 255, 255), // 17
	IM_COL32(255, 255, 255, 255), // 18
	IM_COL32(255, 255, 255, 255), // 19
	IM_COL32(240, 240, 0,   255), // 20
	IM_COL32(240, 240, 0,   255), // 21
	IM_COL32(255, 255, 255, 255), // 22
	IM_COL32(255, 255, 255, 255), // 23
	IM_COL32(255, 255, 255, 255), // 24
	IM_COL32(255, 255, 255, 255), // 25
	IM_COL32(128, 0,   128, 255), // 26
	IM_COL32(255, 255, 255, 255), // 27
	IM_COL32(90,  90,  255, 255), // 28
	IM_COL32(240, 240, 0,   255), // 29
	IM_COL32(0,   140, 0,   255), // 30
	IM_COL32(90,  90,  255, 255), // 31
	IM_COL32(255, 0,   0,   255), // 32
	IM_COL32(90,  90,  255, 255), // 33
	IM_COL32(255, 0,   0,   255), // 34
	IM_COL32(215, 154, 66,  255), // 35
	IM_COL32(110, 143, 176, 255), // 36
	IM_COL32(110, 143, 176, 255), // 37
	IM_COL32(110, 143, 176, 255), // 38
	IM_COL32(110, 143, 176, 255), // 39
	IM_COL32(110, 143, 176, 255), // 40
	IM_COL32(110, 143, 176, 255), // 41
	IM_COL32(110, 143, 176, 255), // 42
	IM_COL32(110, 143, 176, 255), // 43
	IM_COL32(110, 143, 176, 255), // 44
	IM_COL32(110, 143, 176, 255), // 45
	IM_COL32(255, 255, 255, 255), // 46
	IM_COL32(255, 255, 255, 255), // 47
	IM_COL32(255, 0,   0,   255), // 48
	IM_COL32(255, 0,   0,   255), // 49
	IM_COL32(255, 0,   0,   255), // 50
	IM_COL32(255, 0,   0,   255), // 51
	IM_COL32(255, 255, 255, 255), // 52
	IM_COL32(255, 255, 255, 255), // 53
	IM_COL32(255, 255, 255, 255), // 54
	IM_COL32(255, 255, 255, 255), // 55
	IM_COL32(255, 255, 255, 255), // 56
	IM_COL32(255, 255, 255, 255), // 57
	IM_COL32(255, 255, 255, 255), // 58
	IM_COL32(255, 255, 255, 255), // 59
	IM_COL32(215, 154, 66,  255), // 60
	IM_COL32(215, 154, 66,  255), // 61
	IM_COL32(215, 154, 66,  255), // 62
	IM_COL32(215, 154, 66,  255), // 63
	IM_COL32(215, 154, 66,  255), // 64
	IM_COL32(215, 154, 66,  255), // 65
	IM_COL32(215, 154, 66,  255), // 66
	IM_COL32(215, 154, 66,  255), // 67
	IM_COL32(215, 154, 66,  255), // 68
	IM_COL32(215, 154, 66,  255), // 69
	IM_COL32(255, 255, 0,   255), // 70
	IM_COL32(255, 0,   255, 255), // 71
	IM_COL32(0,   200, 200, 255), // 72
	IM_COL32(255, 255, 255, 255), // 73
	IM_COL32(255, 255, 255, 255), // 74
	IM_COL32(0,   255, 255, 255), // 75
	IM_COL32(255, 0,   0,   255), // 76
	IM_COL32(255, 255, 255, 255), // 77
	IM_COL32(90,  90,  255, 255), // 79
	IM_COL32(255, 255, 0,   255), // 70
	IM_COL32(255, 255, 0,   255), // 80
	IM_COL32(255, 255, 255, 255), // 81
	IM_COL32(255, 255, 255, 255), // 82
	IM_COL32(255, 255, 255, 255), // 83
	IM_COL32(255, 255, 255, 255), // 84
	IM_COL32(255, 255, 255, 255), // 85
	IM_COL32(255, 155, 155, 255), // 86
	IM_COL32(90,  90,  255, 255), // 87
	IM_COL32(255, 255, 255, 255), // 88
	IM_COL32(255, 255, 255, 255), // 89
	IM_COL32(255, 255, 255, 255), // 90
	IM_COL32(255, 255, 255, 255), // 91
	IM_COL32(255, 127, 0,   255), // 92
	IM_COL32(255, 255, 255, 255), // 93
	IM_COL32(255, 255, 255, 255), // 94
	IM_COL32(255, 255, 255, 255), // 95
	IM_COL32(192, 0,   0,   255), // 96
	IM_COL32(0,   255, 0,   255), // 97
	IM_COL32(255, 255, 0,   255), // 98
	IM_COL32(255, 0,   0,   255), // 99
	IM_COL32(24,  224, 255, 255), // 100
	IM_COL32(255, 255, 255, 255), // 101
	IM_COL32(255, 255, 255, 255), // 102
	IM_COL32(255, 255, 255, 255), // 103
	IM_COL32(255, 0,   0,   255), // 104
	IM_COL32(255, 0,   0,   255), // 105
};

static ImU32 GetColorForChatColor(DWORD chatColor)
{
	if (chatColor > 255)
	{
		chatColor -= 256;
		if (chatColor >= lengthof(userColors))
			return 0;

		return userColors[chatColor];
	}

	switch (chatColor)
	{
	case COLOR_DEFAULT:       // 0
		return IM_COL32(0xf0, 0xf0, 0xf0, 255);

	case COLOR_DARKGREEN:     // 2 - CONCOLOR_GREEN
		return IM_COL32(0x00, 0x80, 0x00, 255);

	case CONCOLOR_BLUE:       // 4
		return IM_COL32(0x00, 0x40, 0xff, 255);
	case COLOR_PURPLE:        // 5
		return IM_COL32(0xf0, 0x00, 0xf0, 255);
	case COLOR_LIGHTGREY:     // 6 - CONCOLOR_GREY
		return IM_COL32(0x80, 0x80, 0x80, 255);
	case 7: // light gray
		return IM_COL32(0xe0, 0xe0, 0xe0, 255);

	case CONCOLOR_WHITE:      // 10
		return IM_COL32(0xf0, 0xf0, 0xf0, 255);

	case 12: // light gray
		return IM_COL32(0xa0, 0xa0, 0xa0, 255);
	case CONCOLOR_RED:        // 13
		return IM_COL32(0xf0, 0x00, 0x00, 255);
	case 14: // light green
		return IM_COL32(0x00, 0xf0, 0x00, 255);
	case CONCOLOR_YELLOW:     // 15
		return IM_COL32(0xf0, 0xf0, 0x00, 255);
	case 16: // blue
		return IM_COL32(0x00, 0x00, 0xf0, 255);
	case 17: // dark blue
		return IM_COL32(0x00, 0x00, 0xaf, 255);
	case CONCOLOR_LIGHTBLUE:  // 18
		return IM_COL32(0x00, 0xf0, 0xf0, 255);

	case CONCOLOR_BLACK:      // 20
		return IM_COL32(0, 0, 0, 255);
	case 21: // orange
		return IM_COL32(0xf0, 0xa0, 0x00, 255);
	case 22: // brown
		return IM_COL32(0x80, 0x60, 0x20, 255);

	default:
		return IM_COL32(0x60, 0x60, 0x60, 0xff);
	}
}

static DWORD WriteChatColorImGuiAPI(const char* line, DWORD color, DWORD filter)
{
	ImU32 col = GetColorForChatColor(color);
	gImGuiConsole.AddWriteChatColorLog(line, col, true);
	return 0;
}

static void InitializeMQ2ImGuiAPI()
{
	// Init settings
	gbShowDemoWindow = GetPrivateProfileBool("MacroQuest", "ShowDemoWindow", gbShowDemoWindow, mq::internal_paths::MQini);
	gbShowDebugWindow = GetPrivateProfileBool("MacroQuest", "ShowDebugWindow", gbShowDebugWindow, mq::internal_paths::MQini);

	if (gbWriteAllConfig)
	{
		WritePrivateProfileBool("MacroQuest", "ShowDemoWindow", gbShowDemoWindow, mq::internal_paths::MQini);
		WritePrivateProfileBool("MacroQuest", "ShowDebugWindow", gbShowDebugWindow, mq::internal_paths::MQini);
	}

	// Add keybind to toggle imgui
	AddMQ2KeyBind("TOGGLE_IMGUI_OVERLAY", DoToggleImGuiOverlay);

	AddCascadeMenuItem("Settings", []() { gbShowSettingsWindow = true; }, 2);
	AddCascadeMenuItem("Debug Window", []() { gbShowDebugWindow = true; });
	AddCascadeMenuItem("ImGui Demo", []() { gbShowDemoWindow = true; });
}

static void ShutdownMQ2ImGuiAPI()
{
	RemoveMQ2KeyBind("TOGGLE_IMGUI_OVERLAY");

	// Remove the imgui render function
	RemoveRenderCallbacks(gRenderCallbacksId);
}

static void PulseMQ2ImGuiAPI()
{
	static bool bShowDebugWindowLast = gbShowDebugWindow;
	if (bShowDebugWindowLast != gbShowDebugWindow)
	{
		WritePrivateProfileBool("MacroQuest", "ShowDebugWindow", gbShowDebugWindow, mq::internal_paths::MQini);
		bShowDebugWindowLast = gbShowDebugWindow;
	}

	static bool bShowDemoWindowLast = gbShowDemoWindow;
	if (bShowDemoWindowLast != gbShowDemoWindow)
	{
		WritePrivateProfileBool("MacroQuest", "ShowDemoWindow", gbShowDemoWindow, mq::internal_paths::MQini);
		bShowDemoWindowLast = gbShowDemoWindow;
	}
}

} // namespace mq
