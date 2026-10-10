#include "PatchInstaller.h"

#include "NPKFile.h"
#include "PatchResources.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shlobj.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	constexpr char kVersionMarker[] = ".russificator_version";
	constexpr char kDefaultGamePath[] =
		"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Super Mecha Champions";
	constexpr uint64_t kPRes1OriginalSizeBytes = 87402532;

	struct ArchiveChange
	{
		fs::path archive;
		fs::path staged;
		fs::path backup;
	};

	struct PatchResult
	{
		bool success = false;
		bool restored = false;
		std::string error;
	};

	struct ProgressWindowState
	{
		std::atomic<bool> finished = false;
		std::thread worker;
		PatchResult result;
	};

	constexpr UINT_PTR kProgressTimerId = 1;
	constexpr UINT kProgressTimerIntervalMs = 100;

	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			const fs::path base = fs::temp_directory_path();
			const auto timestamp =
				std::chrono::steady_clock::now().time_since_epoch().count();

			for (unsigned int attempt = 0; attempt < 100; ++attempt)
			{
				path_ = base /
					("SMCNPKPatch-" + std::to_string(GetCurrentProcessId()) +
					 "-" + std::to_string(timestamp) + "-" +
					 std::to_string(attempt));

				std::error_code error;
				if (fs::create_directory(path_, error))
					return;
				if (error)
					throw fs::filesystem_error(
						"Could not create a temporary directory",
						path_,
						error);
			}

			throw std::runtime_error(
				"Could not find a unique temporary directory.");
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			fs::remove_all(path_, error);
			if (error)
			{
				std::cerr << "Warning: could not remove temporary directory "
					<< path_.string() << ": " << error.message() << '\n';
			}
		}

		const fs::path& path() const
		{
			return path_;
		}

	private:
		fs::path path_;
	};

	std::string Trim(std::string value)
	{
		const std::string whitespace = " \t\r\n";
		const auto first = value.find_first_not_of(whitespace);
		if (first == std::string::npos)
			return {};

		const auto last = value.find_last_not_of(whitespace);
		value = value.substr(first, last - first + 1);
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.size() - 2);
		return value;
	}

	void ExtractEmbeddedFiles(const fs::path& root)
	{
		for (const auto& resource : PatchResources::files)
		{
			const fs::path relative(resource.path);
			const std::string category = relative.begin()->string();
			if (category != "res" && category != "original")
				throw std::runtime_error(
					"Invalid embedded patch resource path: " +
					std::string(resource.path));

			auto part = relative.begin();
			++part;
			fs::path destination = root / category;
			for (; part != relative.end(); ++part)
				destination /= *part;

			const HRSRC found = FindResourceW(
				nullptr,
				MAKEINTRESOURCEW(static_cast<WORD>(resource.id)),
				MAKEINTRESOURCEW(10));
			if (found == nullptr)
				throw std::runtime_error(
					"An embedded patch resource is missing.");

			const DWORD size = SizeofResource(nullptr, found);
			const HGLOBAL loaded = LoadResource(nullptr, found);
			if (loaded == nullptr)
				throw std::runtime_error(
					"Could not load an embedded patch resource.");

			const void* data = LockResource(loaded);
			if (size != 0 && data == nullptr)
				throw std::runtime_error(
					"Could not read an embedded patch resource.");

			fs::create_directories(destination.parent_path());
			std::ofstream output(destination, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error(
					"Could not create a temporary patch resource: " +
					destination.string());

			if (size != 0)
				output.write(
					static_cast<const char*>(data),
					static_cast<std::streamsize>(size));
			if (!output)
				throw std::runtime_error(
					"Could not write a temporary patch resource: " +
					destination.string());
		}
	}

	std::string ReadVersion(const fs::path& marker)
	{
		std::ifstream input(marker);
		if (!input)
			throw std::runtime_error(
				"Could not read the existing patch version marker.");

		std::string version;
		std::getline(input, version);
		if (!input.eof() && input.fail())
			throw std::runtime_error(
				"Could not read the existing patch version marker.");
		version = Trim(version);
		if (version.empty())
			throw std::runtime_error(
				"The existing patch version marker is empty.");
		return version;
	}

	std::vector<ArchiveChange> StageArchives(
		const fs::path& gameRoot,
		const fs::path& payload,
		const std::string& token)
	{
		const std::vector<fs::path> relativeArchives = {
			fs::path("package") / "res.npk",
			fs::path("package") / "res3.npk",
			fs::path("package") / "patch_npk" / "p_res_1.npk"
		};

		std::vector<ArchiveChange> changes;
		try
		{
			for (const fs::path& relative : relativeArchives)
			{
				const fs::path archive = gameRoot / relative;
				if (!fs::is_regular_file(archive))
					throw std::runtime_error(
						"Game archive is missing: " + archive.string());

				ArchiveChange change{
					archive,
					fs::path(archive.string() + ".smc-patch-" + token + ".tmp"),
					fs::path(archive.string() + ".smc-patch-" + token + ".bak")
				};
				if (fs::exists(change.staged) || fs::exists(change.backup))
					throw std::runtime_error(
						"Temporary patch file already exists beside: " +
						archive.string());

				changes.push_back(std::move(change));
				const uint32_t patched = NPK::Patch(
					archive.string(),
					payload.string(),
					changes.back().staged.string(),
					relative == fs::path("package") / "patch_npk" /
						"p_res_1.npk"
						? kPRes1OriginalSizeBytes
						: 0);
				if (patched == 0)
					throw std::runtime_error(
						"No patch files matched entries in: " +
						archive.string());

				std::cout << "Prepared " << relative.string()
					<< " (" << patched << " file(s)).\n";
			}
		}
		catch (...)
		{
			for (const auto& change : changes)
			{
				std::error_code error;
				fs::remove(change.staged, error);
			}
			throw;
		}

		return changes;
	}

	void CommitArchives(const std::vector<ArchiveChange>& changes)
	{
		std::size_t backedUp = 0;
		std::size_t installed = 0;
		try
		{
			for (const auto& change : changes)
			{
				fs::rename(change.archive, change.backup);
				++backedUp;
				fs::rename(change.staged, change.archive);
				++installed;
			}
		}
		catch (const std::exception& commitError)
		{
			std::string rollbackErrors;
			for (std::size_t index = backedUp; index > 0; --index)
			{
				const auto& change = changes[index - 1];
				try
				{
					if (index <= installed && fs::exists(change.archive))
						fs::remove(change.archive);
					fs::rename(change.backup, change.archive);
				}
				catch (const std::exception& rollbackError)
				{
					rollbackErrors += "\nCould not restore " +
						change.archive.string() + ": " + rollbackError.what();
				}
			}

			throw std::runtime_error(
				std::string("Could not replace game archives: ") +
				commitError.what() + rollbackErrors);
		}

		for (const auto& change : changes)
		{
			std::error_code error;
			fs::remove(change.backup, error);
			if (error)
				std::cerr << "Warning: could not remove backup "
					<< change.backup.string() << ": "
					<< error.message() << '\n';
		}
	}

	void WriteVersionMarker(const fs::path& marker, const std::string& token)
	{
		const fs::path staged(marker.string() + ".tmp-" + token);
		const fs::path backup(marker.string() + ".bak-" + token);
		{
			std::ofstream output(staged, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error(
					"Could not write the patch version marker.");
			output << PatchResources::version << '\n';
			if (!output)
				throw std::runtime_error(
					"Could not write the patch version marker.");
		}

		bool movedExisting = false;
		try
		{
			if (fs::exists(marker))
			{
				fs::rename(marker, backup);
				movedExisting = true;
			}
			fs::rename(staged, marker);
		}
		catch (...)
		{
			if (movedExisting)
			{
				std::error_code restoreError;
				fs::rename(backup, marker, restoreError);
				if (restoreError)
					throw std::runtime_error(
						"Could not update the version marker or restore its "
						"previous copy: " + restoreError.message());
			}
			throw;
		}

		if (movedExisting)
		{
			std::error_code error;
			fs::remove(backup, error);
			if (error)
				std::cerr << "Warning: could not remove old version marker "
					<< backup.string() << ": " << error.message() << '\n';
		}
	}

	bool ApplyPatch(const fs::path& gameRoot)
	{
		if (!fs::is_directory(gameRoot))
			throw std::runtime_error(
				"Game folder does not exist: " + gameRoot.string());

		const fs::path marker = gameRoot / kVersionMarker;
		const bool hasMarker = fs::exists(marker);
		const bool undo = hasMarker &&
			ReadVersion(marker) == PatchResources::version;

		if (PatchResources::replacementCount == 0 ||
			PatchResources::originalCount == 0)
			throw std::runtime_error(
				"The executable does not contain both patch and original "
				"files. Rebuild it after filling Release/res and "
				"Release/original.");

		std::cout << (undo
			? "This patch version is already installed. Restoring originals...\n"
			: hasMarker
				? "Updating the installed patch without restoring the game...\n"
				: "Installing the patch...\n");

		TemporaryDirectory temporary;
		ExtractEmbeddedFiles(temporary.path());
		const fs::path payload = temporary.path() /
			(undo ? "original" : "res");

		const auto token = std::to_string(GetCurrentProcessId()) + "-" +
			std::to_string(
				std::chrono::steady_clock::now().time_since_epoch().count());
		const auto changes = StageArchives(gameRoot, payload, token);
		try
		{
			CommitArchives(changes);
		}
		catch (...)
		{
			for (const auto& change : changes)
			{
				std::error_code error;
				fs::remove(change.staged, error);
			}
			throw;
		}

		if (undo)
		{
			if (!fs::remove(marker))
				throw std::runtime_error(
					"Original files were restored, but the patch marker "
					"could not be removed. Run this executable again.");
			std::cout << "Original files restored. Patch removed.\n";
		}
		else
		{
			WriteVersionMarker(marker, token);
			std::cout << "Patch version " << PatchResources::version
				<< " installed successfully.\n";
		}
		return undo;
	}

	LRESULT CALLBACK ProgressWindowProc(
		HWND window,
		UINT message,
		WPARAM wParam,
		LPARAM lParam)
	{
		auto* state = reinterpret_cast<ProgressWindowState*>(
			GetWindowLongPtrW(window, GWLP_USERDATA));

		if (message == WM_NCCREATE)
		{
			const auto* createInfo =
				reinterpret_cast<const CREATESTRUCTW*>(lParam);
			state = static_cast<ProgressWindowState*>(
				createInfo->lpCreateParams);
			SetWindowLongPtrW(
				window,
				GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(state));
		}

		switch (message)
		{
		case WM_CREATE:
		{
			const HWND label = CreateWindowExW(
				0,
				L"STATIC",
				L"Патчим файлы игры. Не закрывайте это окно.",
				WS_CHILD | WS_VISIBLE,
				20,
				20,
				390,
				24,
				window,
				nullptr,
				GetModuleHandleW(nullptr),
				nullptr);
			const HWND progress = CreateWindowExW(
				0,
				PROGRESS_CLASSW,
				nullptr,
				WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
				20,
				56,
				390,
				22,
				window,
				nullptr,
				GetModuleHandleW(nullptr),
				nullptr);

			if (label == nullptr || progress == nullptr ||
				SetTimer(window, kProgressTimerId,
					kProgressTimerIntervalMs, nullptr) == 0)
				return -1;

			SendMessageW(
				progress,
				PBM_SETMARQUEE,
				TRUE,
				kProgressTimerIntervalMs);
			return 0;
		}

		case WM_TIMER:
			if (wParam == kProgressTimerId && state != nullptr &&
				state->finished.load(std::memory_order_acquire))
			{
				KillTimer(window, kProgressTimerId);
				if (state->worker.joinable())
					state->worker.join();
				DestroyWindow(window);
			}
			return 0;

		case WM_CLOSE:
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}

		return DefWindowProcW(window, message, wParam, lParam);
	}

	PatchResult RunPatchWithProgress(const fs::path& gameRoot)
	{
		INITCOMMONCONTROLSEX controls{};
		controls.dwSize = sizeof(controls);
		controls.dwICC = ICC_PROGRESS_CLASS;
		if (!InitCommonControlsEx(&controls))
			throw std::runtime_error(
				"Could not initialize the patch progress indicator.");

		const wchar_t className[] = L"SMCNPKPatchProgressWindow";
		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = ProgressWindowProc;
		windowClass.hInstance = GetModuleHandleW(nullptr);
		windowClass.hCursor = LoadCursorW(
			nullptr,
			MAKEINTRESOURCEW(32512));
		windowClass.hbrBackground =
			reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		windowClass.lpszClassName = className;
		if (RegisterClassExW(&windowClass) == 0 &&
			GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			throw std::runtime_error(
				"Could not create the patch progress window.");

		ProgressWindowState state;
		const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
		RECT bounds{ 0, 0, 430, 105 };
		AdjustWindowRect(&bounds, style, FALSE);
		HWND window = CreateWindowExW(
			WS_EX_DLGMODALFRAME,
			className,
			L"Русификатор SMC (Steam)",
			style,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			bounds.right - bounds.left,
			bounds.bottom - bounds.top,
			nullptr,
			nullptr,
			windowClass.hInstance,
			&state);
		if (window == nullptr)
			throw std::runtime_error(
				"Could not open the patch progress window.");

		state.worker = std::thread([&state, gameRoot]()
		{
			try
			{
				state.result.restored = ApplyPatch(gameRoot);
				state.result.success = true;
			}
			catch (const std::exception& error)
			{
				state.result.error = error.what();
			}
			state.finished.store(true, std::memory_order_release);
		});

		ShowWindow(window, SW_SHOWNORMAL);
		UpdateWindow(window);

		MSG message{};
		int messageResult = 0;
		while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		if (messageResult == -1)
		{
			if (IsWindow(window))
				DestroyWindow(window);
			if (state.worker.joinable())
				state.worker.join();
			throw std::runtime_error(
				"An error occurred while displaying patch progress.");
		}

		if (state.worker.joinable())
			state.worker.join();
		return state.result;
	}

	int CALLBACK SetInitialFolder(
		HWND dialog,
		UINT message,
		LPARAM,
		LPARAM initialFolder)
	{
		if (message == BFFM_INITIALIZED)
			SendMessageW(
				dialog,
				BFFM_SETSELECTIONW,
				TRUE,
				initialFolder);
		return 0;
	}
}

int RunPatchInstaller()
{
	const fs::path defaultPath(kDefaultGamePath);
	const std::wstring title =
		L"Выберите папку Super Mecha Champions.";
	BROWSEINFOW browseInfo{};
	browseInfo.hwndOwner = nullptr;
	browseInfo.lpszTitle = title.c_str();
	browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	browseInfo.lpfn = SetInitialFolder;
	browseInfo.lParam = reinterpret_cast<LPARAM>(
		defaultPath.c_str());

	const HRESULT comResult =
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(comResult))
	{
		MessageBoxW(
			nullptr,
			L"Не удалось запустить диалог выбора папки.",
			L"Русификатор SMC (Steam)",
			MB_OK | MB_ICONERROR);
		return 1;
	}

	PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browseInfo);
	if (selected == nullptr)
	{
		CoUninitialize();
		return 0;
	}

	std::array<wchar_t, MAX_PATH> selectedPath{};
	if (!SHGetPathFromIDListW(selected, selectedPath.data()))
	{
		CoTaskMemFree(selected);
		CoUninitialize();
		MessageBoxW(
			nullptr,
			L"Не удалось прочитать выбранную папку.",
			L"Русификатор SMC (Steam)",
			MB_OK | MB_ICONERROR);
		return 1;
	}
	CoTaskMemFree(selected);
	CoUninitialize();

	PatchResult result;
	try
	{
		result = RunPatchWithProgress(fs::path(selectedPath.data()));
	}
	catch (const std::exception& error)
	{
		MessageBoxA(
			nullptr,
			(std::string("Patch failed:\n") + error.what()).c_str(),
			"Русификатор SMC (Steam)",
			MB_OK | MB_ICONERROR);
		return 1;
	}

	if (!result.success)
	{
		MessageBoxA(
			nullptr,
			(std::string("Patch failed:\n") + result.error).c_str(),
			"Русификатор SMC (Steam)",
			MB_OK | MB_ICONERROR);
		return 1;
	}

	MessageBoxW(
		nullptr,
		result.restored
			? L"Патч отменён. Оригинальные файлы восстановлены."
			: L"Патч успешно установлен или обновлён.",
		L"Русификатор SMC (Steam)",
		MB_OK | MB_ICONINFORMATION);
	return 0;
}
