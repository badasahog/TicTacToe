/*
* (C) 2023 badasahog. All Rights Reserved
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*/

#include <Windows.h>
#include <wrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sstream>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

#if !_HAS_CXX20
#error C++20 is required
#endif

#ifndef __has_cpp_attribute
#error critical macro __has_cpp_attribute not defined
#endif

#if !__has_include(<Windows.h>)
#error critital header Windows.h not found
#endif

HWND Window;

inline void FATAL_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			nullptr
		);

		wchar_t buffer[256];

		if (formattedErrorLength == 0)
		{
			_snwprintf_s(buffer, 256, _TRUNCATE, L"an error occured, unable to retrieve error message\nerror code: 0x%X\nlocation: line %i\n\0", hr, line);
			ShowWindow(Window, SW_HIDE);
			MessageBoxW(Window, buffer, L"Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
		}
		else
		{
			_snwprintf_s(buffer, 256, _TRUNCATE, L"an error occured: %s\nerror code: 0x%X\nlocation: line %i\n\0", messageBuffer, hr, line);
			ShowWindow(Window, SW_HIDE);
			MessageBoxW(Window, buffer, L"Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
		}
		ExitProcess(EXIT_FAILURE);
	}
}

#define FATAL_ON_FAIL(x) FATAL_ON_FAIL_IMPL(x, __LINE__)

#define FATAL_ON_FALSE(x) if((x) == FALSE) FATAL_ON_FAIL(GetLastError())

#define VALIDATE_HANDLE(x) if((x) == nullptr || (x) == INVALID_HANDLE_VALUE) FATAL_ON_FAIL(GetLastError())

using Microsoft::WRL::ComPtr;

ComPtr<ID2D1Factory> factory;
ComPtr<ID2D1HwndRenderTarget> renderTarget;

ComPtr<ID2D1SolidColorBrush> brush;
ComPtr<ID2D1SolidColorBrush> PlayerBrush;
ComPtr<ID2D1SolidColorBrush> CPUBrush;
ComPtr<ID2D1SolidColorBrush> GhostBrush;

ComPtr<IDWriteFactory> pDWriteFactory;

ComPtr<IDWriteTextFormat> TitleTextFormat;
ComPtr<IDWriteTextFormat> pTextFormat;
ComPtr<IDWriteTextFormat> CopyrightTextFormat;


LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept;


char boardState[9] = { 0 };

int mouseInSquare = 9;

bool mouseClicked = false;

int gameState = 0;

int winType = 0;

int playerScore = 0;
int CPUScore = 0;

int CPUMoveCount = 0;

LARGE_INTEGER CPUThinkingTicks;
LARGE_INTEGER GameFinishedTicks;
LARGE_INTEGER CurrentTimerFinished;

int windowWidth = 0;
int windowHeight = 0;


[[nodiscard]]
int CheckForWinner() noexcept
{
	//horizontal
	if (boardState[0] == boardState[1] && boardState[1] == boardState[2])
		return 1;

	if (boardState[3] == boardState[4] && boardState[4] == boardState[5])
		return 2;

	if (boardState[6] == boardState[7] && boardState[7] == boardState[8])
		return 3;

	//vertical
	if (boardState[0] == boardState[3] && boardState[3] == boardState[6])
		return 4;

	if (boardState[1] == boardState[4] && boardState[4] == boardState[7])
		return 5;

	if (boardState[2] == boardState[5] && boardState[5] == boardState[8])
		return 6;

	//diagonal
	if (boardState[0] == boardState[4] && boardState[4] == boardState[8])
		return 7;

	if (boardState[6] == boardState[4] && boardState[4] == boardState[2])
		return 8;

	return 0;
}

void CreateAssets() noexcept
{
	RECT ClientRect;
	FATAL_ON_FALSE(GetClientRect(Window, &ClientRect));

	D2D1_SIZE_U size = D2D1::SizeU(ClientRect.right, ClientRect.bottom);

	FATAL_ON_FAIL(factory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(),
		D2D1::HwndRenderTargetProperties(Window, size),
		&renderTarget));

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 0.0f), &brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f), &PlayerBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f), &CPUBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(.564f, .564f, .564f), &GhostBrush));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.12f * windowHeight,
		L"en-us",
		&TitleTextFormat
	));

	FATAL_ON_FAIL(TitleTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.08f * windowHeight,
		L"en-us",
		&pTextFormat
	));

	FATAL_ON_FAIL(pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.05f * windowHeight,
		L"en-us",
		&CopyrightTextFormat
	));

	FATAL_ON_FAIL(CopyrightTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
}

void DrawMenu() noexcept
{
	if (renderTarget == nullptr)
	{
		CreateAssets();
	}

	renderTarget->BeginDraw();
	renderTarget->Clear();

	{
		//title
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .1f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L" TIC          TOE", 17, TitleTextFormat.Get(), textArea, PlayerBrush.Get());
		renderTarget->DrawTextW(L"TAC", 3, TitleTextFormat.Get(), textArea, CPUBrush.Get());
	}

	POINT cursorPos;
	FATAL_ON_FALSE(GetCursorPos(&cursorPos));
	FATAL_ON_FALSE(ScreenToClient(Window, &cursorPos));

	{
		//play button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .3f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"PLAY", 4, pTextFormat.Get(), textArea, GhostBrush.Get());
	}

	{
		//exit button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .45f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"EXIT", 4, pTextFormat.Get(), textArea, GhostBrush.Get());
	}

	{
		//copyright
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .9f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * 1.f
		};
		renderTarget->DrawTextW(L"\u24B8 2023 badasahog. All Rights Reserved", 37, CopyrightTextFormat.Get(), textArea, GhostBrush.Get());
	}

	if (cursorPos.x > windowWidth * .4f &&
		cursorPos.x < windowWidth * .6f &&
		cursorPos.y > windowHeight * .3f &&
		cursorPos.y < windowHeight * .4f)
	{
		//play button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .3f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};

		renderTarget->DrawTextW(L"PLAY", 4, pTextFormat.Get(), textArea, brush.Get());

		if (mouseClicked)
		{
			gameState = 1;
		}
	}
	else if (
		cursorPos.x > windowWidth * .4f &&
		cursorPos.x < windowWidth * .6f &&
		cursorPos.y > windowHeight * .45f &&
		cursorPos.y < windowHeight * .55f)
	{
		//exit button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .45f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"EXIT", 4, pTextFormat.Get(), textArea, brush.Get());

		if (mouseClicked)
		{
			ExitProcess(EXIT_SUCCESS);
		}
	}

	mouseClicked = false;

	FATAL_ON_FAIL(renderTarget->EndDraw());
}

void DrawGame() noexcept
{

	if (renderTarget == nullptr)
	{
		CreateAssets();
	}

	renderTarget->BeginDraw();
	renderTarget->Clear();

	FLOAT ScoreWidth = .2 * windowWidth;

	{
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = 0,
			.right = (FLOAT)windowWidth / 2 - ScoreWidth,
			.bottom = (.1f / .5f) * windowHeight
		};
		renderTarget->DrawTextW(L"YOU", 3, pTextFormat.Get(), textArea, PlayerBrush.Get());
	}

	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2 - ScoreWidth,
			.top = 0,
			.right = (FLOAT)windowWidth / 2,
			.bottom = (.1f / .5f) * windowHeight
		};

		std::wstring scoreText = std::to_wstring(playerScore);
		renderTarget->DrawTextW(scoreText.c_str(), scoreText.length(), pTextFormat.Get(), textArea, PlayerBrush.Get());
	}


	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2 + ScoreWidth,
			.top = 0,
			.right = (FLOAT)windowWidth,
			.bottom = (.1f / .5f) * windowHeight
		};
		renderTarget->DrawTextW(L"CPU", 3, pTextFormat.Get(), textArea, CPUBrush.Get());
	}

	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2,
			.top = 0,
			.right = (FLOAT)windowWidth / 2 + ScoreWidth,
			.bottom = (.1f / .5f) * windowHeight
		};

		std::wstring scoreText = std::to_wstring(CPUScore);
		renderTarget->DrawTextW(scoreText.c_str(), scoreText.length(), pTextFormat.Get(), textArea, CPUBrush.Get());
	}


	D2D1_RECT_F boardArea =
	{
		.left = .205f / 2 * (FLOAT)windowWidth,
		.top = (.1f / .8f) * windowHeight,
		.right = (FLOAT)windowWidth - .205f / 2 * (FLOAT)windowWidth,
		.bottom = (FLOAT)windowHeight - .08f * windowHeight
	};

	float boardWidth = boardArea.right - boardArea.left;

	float lineWidth = (FLOAT)windowWidth * .02f;

	float squareSize = (boardWidth - lineWidth * 2) / 3.f;

	//draw the board

	{
		//first line
		D2D1_RECT_F boardSquare =
		{
			.left = boardArea.left + squareSize,
			.top = boardArea.top,
			.right = boardArea.left + squareSize + lineWidth,
			.bottom = boardArea.bottom
		};
		renderTarget->FillRectangle(boardSquare, brush.Get());
	}

	{
		//second line
		D2D1_RECT_F boardSquare =
		{
			.left = boardArea.left + squareSize * 2 + lineWidth,
			.top = boardArea.top,
			.right = boardArea.left + squareSize * 2 + lineWidth * 2,
			.bottom = boardArea.bottom
		};
		renderTarget->FillRectangle(boardSquare, brush.Get());
	}

	{
		//third line
		D2D1_RECT_F boardSquare =
		{
			.left = boardArea.left,
			.top = boardArea.top + squareSize,
			.right = boardArea.right,
			.bottom = boardArea.top + squareSize + lineWidth
		};

		renderTarget->FillRectangle(boardSquare, brush.Get());
	}

	{
		//fourth line
		D2D1_RECT_F boardSquare =
		{
			.left = boardArea.left,
			.top = boardArea.top + squareSize * 2 + lineWidth,
			.right = boardArea.right,
			.bottom = boardArea.top + squareSize * 2 + lineWidth * 2
		};

		renderTarget->FillRectangle(boardSquare, brush.Get());
	}

	D2D1_POINT_2F squarePoints[9] =
	{
		{ boardArea.left + squareSize * 0 + lineWidth * 0, boardArea.top + squareSize * 0 + lineWidth * 0 },
		{ boardArea.left + squareSize * 1 + lineWidth * 1, boardArea.top + squareSize * 0 + lineWidth * 0 },
		{ boardArea.left + squareSize * 2 + lineWidth * 2, boardArea.top + squareSize * 0 + lineWidth * 0 },

		{ boardArea.left + squareSize * 0 + lineWidth * 0, boardArea.top + squareSize * 1 + lineWidth * 1 },
		{ boardArea.left + squareSize * 1 + lineWidth * 1, boardArea.top + squareSize * 1 + lineWidth * 1 },
		{ boardArea.left + squareSize * 2 + lineWidth * 2, boardArea.top + squareSize * 1 + lineWidth * 1 },

		{ boardArea.left + squareSize * 0 + lineWidth * 0, boardArea.top + squareSize * 2 + lineWidth * 2 },
		{ boardArea.left + squareSize * 1 + lineWidth * 1, boardArea.top + squareSize * 2 + lineWidth * 2 },
		{ boardArea.left + squareSize * 2 + lineWidth * 2, boardArea.top + squareSize * 2 + lineWidth * 2 }
	};

	//draw the pieces (or whatever they're called)

	for (int i = 0; i < 9; i++)
	{
		switch (boardState[i])
		{
		case 1://O
		{
			D2D1_ELLIPSE circle =
			{
				.point =
				{
					.x = squarePoints[i].x + squareSize / 2,
					.y = squarePoints[i].y + squareSize / 2,
				},
				.radiusX = squareSize * .3f,
				.radiusY = squareSize * .3f,
			};
			renderTarget->DrawEllipse(circle, CPUBrush.Get(), squareSize * .15f);
			break;
		}
		case 2://X
		{
			{
				//first line
				D2D1_POINT_2F firstPoint =
				{
					.x = squarePoints[i].x + squareSize * .08f,
					.y = squarePoints[i].y + squareSize * .08f
				};

				D2D1_POINT_2F secondPoint =
				{
					.x = squarePoints[i].x + squareSize - squareSize * .08f,
					.y = squarePoints[i].y + squareSize - squareSize * .08f
				};

				renderTarget->DrawLine(firstPoint, secondPoint, PlayerBrush.Get(), squareSize * .15f);
			}

			{
				//second line
				D2D1_POINT_2F firstPoint =
				{
					.x = squarePoints[i].x + squareSize - squareSize * .08f,
					.y = squarePoints[i].y + squareSize * .08f
				};

				D2D1_POINT_2F secondPoint =
				{
					.x = squarePoints[i].x + squareSize * .08f,
					.y = squarePoints[i].y + squareSize - squareSize * .08f
				};

				renderTarget->DrawLine(firstPoint, secondPoint, PlayerBrush.Get(), squareSize * .15f);
			}

			break;
		}
		}
	}

	if (gameState == 1)
	{
		POINT cursorPos;
		FATAL_ON_FALSE(GetCursorPos(&cursorPos));
		FATAL_ON_FALSE(ScreenToClient(Window, &cursorPos));

		mouseInSquare = 9;

		if (cursorPos.x > boardArea.left &&
			cursorPos.x < boardArea.right &&
			cursorPos.y > boardArea.top &&
			cursorPos.y < boardArea.bottom)
		{
			for (int i = 0; i < 9; i++)
			{
				if (cursorPos.x > squarePoints[i].x &&
					cursorPos.x < squarePoints[i].x + squareSize &&
					cursorPos.y > squarePoints[i].y &&
					cursorPos.y < squarePoints[i].y + squareSize)
				{
					mouseInSquare = i;
					break;
				}
			}
		}

		if (mouseInSquare != 9)
		{
			if (boardState[mouseInSquare] < 0)
			{

				{
					//first line
					D2D1_POINT_2F firstPoint =
					{
						.x = squarePoints[mouseInSquare].x + squareSize * .08f,
						.y = squarePoints[mouseInSquare].y + squareSize * .08f
					};

					D2D1_POINT_2F secondPoint =
					{
						.x = squarePoints[mouseInSquare].x + squareSize - squareSize * .08f,
						.y = squarePoints[mouseInSquare].y + squareSize - squareSize * .08f
					};

					renderTarget->DrawLine(firstPoint, secondPoint, GhostBrush.Get(), squareSize * .15f);
				}

				{
					//second line
					D2D1_POINT_2F firstPoint =
					{
						.x = squarePoints[mouseInSquare].x + squareSize - squareSize * .08f,
						.y = squarePoints[mouseInSquare].y + squareSize * .08f
					};

					D2D1_POINT_2F secondPoint =
					{
						.x = squarePoints[mouseInSquare].x + squareSize * .08f,
						.y = squarePoints[mouseInSquare].y + squareSize - squareSize * .08f
					};

					renderTarget->DrawLine(firstPoint, secondPoint, GhostBrush.Get(), squareSize * .15f);
				}

				if (mouseClicked)
				{
					boardState[mouseInSquare] = 2;

					winType = CheckForWinner();

					if (winType != 0)
					{
						playerScore++;
						playerScore = min(playerScore, 999);
						gameState = 3;
						LARGE_INTEGER tickCountNow;
						FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
						CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + GameFinishedTicks.QuadPart;
					}
					else
					{
						gameState = 2;
						LARGE_INTEGER tickCountNow;
						FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
						CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + CPUThinkingTicks.QuadPart;
					}
				}
			}
		}
	}
	else if (gameState == 2)
	{
		//CPU's turn
		LARGE_INTEGER tickCountNow;
		FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
		if (CPUMoveCount == 4)
		{
			//tie
			gameState = 3;
		}
		else if (tickCountNow.QuadPart > CurrentTimerFinished.QuadPart)
		{
			int numOpenSpaces = 0;

			int openSpaces[9] = { 0 };

			for (int i = 0; i < 9; i++)
			{
				if (boardState[i] < 0)
				{
					openSpaces[numOpenSpaces] = i;
					numOpenSpaces++;
				}
			}

			boardState[openSpaces[rand() % numOpenSpaces]] = 1;

			CPUMoveCount++;

			winType = CheckForWinner();

			if (winType != 0)
			{
				CPUScore++;
				CPUScore = min(CPUScore, 999);
				gameState = 3;
				LARGE_INTEGER tickCountNow;
				QueryPerformanceCounter(&tickCountNow);
				CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + GameFinishedTicks.QuadPart;
			}
			else
			{
				gameState = 1;
			}
		}
	}
	else if (gameState == 3)
	{
		LARGE_INTEGER tickCountNow;
		QueryPerformanceCounter(&tickCountNow);

		FLOAT winLineMargin = boardWidth * .05f;
		FLOAT winLineWidth = squareSize * .25f;

		switch (winType)
		{
		case 1:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[0].x + winLineMargin,
				.y = squarePoints[0].y + squareSize / 2
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[2].x + squareSize - winLineMargin,
				.y = squarePoints[2].y + squareSize / 2
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 2:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[3].x + winLineMargin,
				.y = squarePoints[3].y + squareSize / 2
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[5].x + squareSize - winLineMargin,
				.y = squarePoints[5].y + squareSize / 2
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 3:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[6].x + winLineMargin,
				.y = squarePoints[6].y + squareSize / 2
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[8].x + squareSize - winLineMargin,
				.y = squarePoints[8].y + squareSize / 2
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 4:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[0].x + squareSize / 2,
				.y = squarePoints[0].y + winLineMargin
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[6].x + squareSize / 2,
				.y = squarePoints[6].y + squareSize - winLineMargin
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 5:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[1].x + squareSize / 2,
				.y = squarePoints[1].y + winLineMargin
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[7].x + squareSize / 2,
				.y = squarePoints[7].y + squareSize - winLineMargin
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 6:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[2].x + squareSize / 2,
				.y = squarePoints[2].y + winLineMargin
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[8].x + squareSize / 2,
				.y = squarePoints[8].y + squareSize - winLineMargin
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 7:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[0].x + winLineMargin,
				.y = squarePoints[0].y + winLineMargin
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[8].x + squareSize - winLineMargin,
				.y = squarePoints[8].y + squareSize - winLineMargin
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		case 8:
		{
			D2D1_POINT_2F firstPoint =
			{
				.x = squarePoints[2].x + squareSize - winLineMargin,
				.y = squarePoints[2].y + winLineMargin
			};

			D2D1_POINT_2F secondPoint =
			{
				.x = squarePoints[6].x + winLineMargin,
				.y = squarePoints[6].y + squareSize - winLineMargin
			};

			renderTarget->DrawLine(firstPoint, secondPoint, brush.Get(), winLineWidth);
			break;
		}
		}

		if (tickCountNow.QuadPart > CurrentTimerFinished.QuadPart)
		{
			boardState[0] = -1;
			boardState[1] = -2;
			boardState[2] = -3;
			boardState[3] = -4;
			boardState[4] = -5;
			boardState[5] = -6;
			boardState[6] = -7;
			boardState[7] = -8;
			boardState[8] = -9;
			gameState = 1;
			CPUMoveCount = 0;
		}
	}

	mouseClicked = false;

	FATAL_ON_FAIL(renderTarget->EndDraw());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	LARGE_INTEGER ProcessorFrequency;
	FATAL_ON_FALSE(QueryPerformanceFrequency(&ProcessorFrequency));

	CPUThinkingTicks.QuadPart = ProcessorFrequency.QuadPart * 1;
	GameFinishedTicks.QuadPart = ProcessorFrequency.QuadPart * 3;

	{
		LARGE_INTEGER tickCountNow;
		FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
		srand(tickCountNow.LowPart);
	}

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	UINT dpi = GetDpiForSystem();

	windowWidth = 6 * dpi;
	windowHeight = 6 * dpi;

	// Register the window class.
	constexpr wchar_t CLASS_NAME[] = L"Window CLass";

	WNDCLASS wc =
	{
		.lpfnWndProc = PreInitProc,
		.hInstance = hInstance,
		.lpszClassName = CLASS_NAME
	};
	RegisterClassW(&wc);

	// Get the required window size
	RECT windowRect =
	{
		.left = 50,
		.top = 50,
		.right = windowWidth + 50,
		.bottom = windowHeight + 50
	};

	FATAL_ON_FALSE(AdjustWindowRect(&windowRect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE));


	// Create the window

	Window = CreateWindowExW(
		WS_EX_CLIENTEDGE,
		CLASS_NAME,
		L"Tic Tac Toe",
		WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		(GetSystemMetrics(SM_CXSCREEN) - (windowRect.right - windowRect.left)) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - (windowRect.bottom - windowRect.top)) / 2,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	VALIDATE_HANDLE(Window);

	FATAL_ON_FAIL(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));

	FATAL_ON_FAIL(DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		&pDWriteFactory
	));

	boardState[0] = -1;
	boardState[1] = -2;
	boardState[2] = -3;
	boardState[3] = -4;
	boardState[4] = -5;
	boardState[5] = -6;
	boardState[6] = -7;
	boardState[7] = -8;
	boardState[8] = -9;

	FATAL_ON_FALSE(ShowWindow(Window, SW_SHOW));

	
	SetWindowLongPtrA(Window, GWLP_WNDPROC, (LONG_PTR)&WindowProc);

	SetCursor(LoadCursorW(NULL, IDC_ARROW));

	// Run the message loop.
	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			FATAL_ON_FALSE(TranslateMessage(&Message));
			DispatchMessageW(&Message);
		}
	}

	return EXIT_SUCCESS;
}

void handleDpiChange() noexcept
{
	UINT dpi = GetDpiForSystem();

	windowWidth = 6 * dpi;
	windowHeight = 6 * dpi;

	RECT windowRect =
	{
		.left = 50,
		.top = 50,
		.right = windowWidth + 50,
		.bottom = windowHeight + 50
	};

	FATAL_ON_FALSE(AdjustWindowRect(&windowRect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE));

	FATAL_ON_FALSE(SetWindowPos(
		Window,
		nullptr,
		0,
		0,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOOWNERZORDER | SWP_NOREPOSITION | SWP_NOSENDCHANGING));
}

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (message)
	{
	case WM_DPICHANGED:
		handleDpiChange();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (message)
	{

	case WM_DPICHANGED:
		handleDpiChange();
		break;
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (!IsIconic(hwnd))
			FATAL_ON_FALSE(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&WindowProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
		mouseClicked = true;
		break;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			gameState = 0;

			boardState[0] = -1;
			boardState[1] = -2;
			boardState[2] = -3;
			boardState[3] = -4;
			boardState[4] = -5;
			boardState[5] = -6;
			boardState[6] = -7;
			boardState[7] = -8;
			boardState[8] = -9;

			playerScore = 0;
			CPUScore = 0;
			CPUMoveCount = 0;
			mouseClicked = false;
		}
		break;
	case WM_DPICHANGED:
		handleDpiChange();
	[[fallthrough]];
	case WM_SIZE:
		if (IsIconic(hwnd))
		{
			FATAL_ON_FALSE(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&IdleProc) != 0);
			break;
		}
		CreateAssets();
	[[fallthrough]];
	case WM_PAINT:
		if (gameState == 0)
			DrawMenu();
		else
			DrawGame();
		break;
	default:
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}
