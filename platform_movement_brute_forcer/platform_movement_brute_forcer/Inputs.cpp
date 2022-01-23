#include "Inputs.hpp"
#include <cstdio>
#include <iostream>
#include "Magic.hpp"
#include "Game.hpp"

std::map<int16_t, std::map<float, std::pair<int8_t, int8_t>>> yawMagToInputs;

void Inputs::set_inputs(Inputs inputs)
{
	uint16_t* buttonDllAddr = (uint16_t*)game.addr("gControllerPads");
	buttonDllAddr[0] = inputs.buttons;

	int8_t* xStickDllAddr = (int8_t*)(game.addr("gControllerPads") + 2);
	xStickDllAddr[0] = inputs.stick_x;

	int8_t* yStickDllAddr = (int8_t*)(game.addr("gControllerPads") + 3);
	yStickDllAddr[0] = inputs.stick_y;
}

void Inputs::PopulateInputMappings()
{
	for (int16_t stickX = -128; stickX <= 127; stickX++)
	{
		for (int16_t stickY = -128; stickY <= 127; stickY++)
		{
			float adjustedStickX = 0;
			float adjustedStickY = 0;

			if (stickX <= -8) 
				adjustedStickX = stickX + 6;
			else if (stickX >= 8) 
				adjustedStickX = stickX - 6;

			if (stickY <= -8) 
				adjustedStickY = stickY + 6;
			else if (stickY >= 8) 
				adjustedStickY = stickY - 6;

			float stickMag = sqrtf(adjustedStickX * adjustedStickX + adjustedStickY * adjustedStickY);

			if (stickMag > 64)
			{
				adjustedStickX *= 64 / stickMag;
				adjustedStickY *= 64 / stickMag;
				stickMag = 64;
			}

			float intendedMag = ((stickMag / 64.0f) * (stickMag / 64.0f)) * 64.0f;
			intendedMag = intendedMag / 2.0f;

			if (intendedMag > 0.0f)
			{
				int16_t baseIntendedYaw = atan2s(-adjustedStickY, adjustedStickX);
				if (yawMagToInputs.count(baseIntendedYaw) == 0)
					yawMagToInputs[baseIntendedYaw] = std::map<float, std::pair<int8_t, int8_t>>{ { intendedMag, std::pair<int8_t, int8_t>(stickX, stickY) } };
				else if (yawMagToInputs[baseIntendedYaw].count(intendedMag) == 0)
					yawMagToInputs[baseIntendedYaw][intendedMag] = std::pair<int8_t, int8_t>(stickX, stickY);
			}
		}
	}
}

std::pair<int8_t, int8_t> Inputs::GetClosestInputByYawHau(int16_t intendedYaw, float intendedMag, int16_t cameraYaw, int32_t direction)
{
	//intendedYaw = baseIntendedYaw + cameraYaw

	if (intendedMag == 0.0f)
		return std::pair(0, 0);

	int16_t minIntendedYaw = intendedYaw - (intendedYaw & 15);
	int16_t maxIntendedYaw = minIntendedYaw + 15;

	int16_t minBaseIntendedYaw = minIntendedYaw - cameraYaw;
	int16_t maxBaseIntendedYaw = maxIntendedYaw - cameraYaw;

	std::pair<int8_t, int8_t> closestInput = std::pair(0, 0);
	float closestMagDistance = INFINITY;

	bool foundMatchingYawHau = false;
	int hauOffset = 0;
	while (true)
	{
		for (int16_t yaw = int16_t(minBaseIntendedYaw + 16 * hauOffset); yaw != int16_t(maxBaseIntendedYaw + 16 * hauOffset); yaw++)
		{
			if (yawMagToInputs.count(yaw))
			{
				foundMatchingYawHau = true;
				if (yawMagToInputs[yaw].count(intendedMag))
					return yawMagToInputs[yaw][intendedMag];

				auto lower = yawMagToInputs[yaw].lower_bound(intendedMag);
				auto upper = yawMagToInputs[yaw].upper_bound(intendedMag);
				float magDistance = INFINITY;
				if (lower == yawMagToInputs[yaw].begin())
				{
					magDistance = (*upper).first - intendedMag;
					if (magDistance < closestMagDistance)
					{
						closestMagDistance = magDistance;
						closestInput = (*upper).second;
					}
				}
				else if (upper == yawMagToInputs[yaw].end())
				{
					magDistance = intendedMag - (*lower).first;
					if (magDistance < closestMagDistance)
					{
						closestMagDistance = magDistance;
						closestInput = (*lower).second;
					}
				}
				else
				{
					float lowerMagDistance = intendedMag - (*lower).first;
					float upperMagDistance = (*upper).first - intendedMag;

					if (lowerMagDistance <= upperMagDistance)
					{
						if (lowerMagDistance < closestMagDistance)
						{
							closestMagDistance = lowerMagDistance;
							closestInput = (*lower).second;
						}
					}
					else
					{
						if (upperMagDistance < closestMagDistance)
						{
							closestMagDistance = upperMagDistance;
							closestInput = (*upper).second;
						}
					}
				}
			}
		}

		//Check both positive and negative HAU offsets for the closest mag if either of them finds a matching yaw
		if (direction == 0)
		{
			if (hauOffset == 0)
			{
				if (foundMatchingYawHau)
					break;
				hauOffset = 1;
			}
			else if (hauOffset > 0)
				hauOffset *= -1;
			else if (foundMatchingYawHau)
				break;
			else
				hauOffset = -hauOffset + 1;
		}
		else if (direction > 0)
		{
			if (foundMatchingYawHau)
				break;
			hauOffset++;
		}
		else
		{
			if (foundMatchingYawHau)
				break;
			hauOffset--;
		}
	}

	return closestInput;
}

int M64::load()
{
	FILE* f;

	uint16_t buttons;
	int8_t stick_x, stick_y;

	size_t err;

	try {
		if ((err = fopen_s(&f, fileName, "rb")) != 0) {
			std::cerr << "Bad open: " << err << std::endl;
			exit(EXIT_FAILURE);
		}

		fseek(f, 0x400, SEEK_SET);

		uint64_t index = 0;
		while (true) {
			uint16_t bigEndianButtons;

			fread(&bigEndianButtons, sizeof(uint16_t), 1, f);

			if (feof(f) != 0 || ferror(f) != 0) {
				break;
			}

			buttons = ntohs(bigEndianButtons);

			fread(&stick_x, sizeof(int8_t), 1, f);

			if (feof(f) != 0 || ferror(f) != 0) {
				break;
			}

			fread(&stick_y, sizeof(int8_t), 1, f);

			if (feof(f) != 0 || ferror(f) != 0) {
				break;
			}

			frames[index] = Inputs(buttons, stick_x, stick_y);
			index++;
		}

		fclose(f);
	}
	catch (std::invalid_argument& e)
	{
		std::cerr << e.what() << std::endl;
		return 0;
	}

	return 1;
}

int M64::save(long initFrame)
{
	FILE* f;

	uint16_t buttons;
	int8_t stick_x, stick_y;

	size_t err;
	uint64_t lastFrame = frames.rbegin()->first;

	try {
		if ((err = fopen_s(&f, fileName, "r+b")) != 0) {
			std::cerr << "Bad open: " << err << std::endl;
			exit(EXIT_FAILURE);
		}

		//Write number of frames
		fseek(f, 0xC, SEEK_SET);
		int32_t nFrames[1] = { -1 }; //To-Do: maybe change to size
		fwrite(nFrames, sizeof(int32_t), 1, f);
		if (ferror(f) != 0)
			return 0;

		//Write frames
		fseek(f, 0x400 + 4 * initFrame, SEEK_SET);
		for (int i = 0; i <= lastFrame; i++) {
			uint16_t bigEndianButtons = 0;
			int8_t stickX = 0;
			int8_t stickY = 0;
			
			if (frames.count(i))
			{
				bigEndianButtons = htons(frames[i].buttons);
				stickX = frames[i].stick_x;
				stickY = frames[i].stick_y;
			}
			
			fwrite(&bigEndianButtons, sizeof(uint16_t), 1, f);
			fwrite(&stickX, sizeof(int8_t), 1, f);
			fwrite(&stickY, sizeof(int8_t), 1, f);
		}

		fclose(f);
	}
	catch (std::invalid_argument& e)
	{
		std::cerr << e.what() << std::endl;
		return 0;
	}

	return 1;
}