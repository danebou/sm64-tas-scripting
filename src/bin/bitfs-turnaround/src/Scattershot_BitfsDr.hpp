#pragma once
#include <Scattershot.hpp>
#include <tasfw/scripts/BitFSPyramidOscillation.hpp>
//#include <tasfw/SharedLib.hpp>
//#include <unordered_set>

class SShotState_BitfsDr
{
public:
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint64_t s;

    SShotState_BitfsDr(uint8_t x, uint8_t y, uint8_t z, uint64_t s) : x(x), y(y), z(z), s(s) { }
};

template <class TState, derived_from_specialization_of<Resource> TResource>
class Scattershot_BitfsDr : public ScattershotThread<TState, TResource>
{
public:
    using Script<TResource>::resource;
    using Script<TResource>::GetCurrentFrame;
    using Script<TResource>::AdvanceFrameWrite;
    using ScattershotThread<TState, TResource>::config;

    enum class MovementOptions
    {
        // Joystick mag
        MAX_MAGNITUDE,
        ZERO_MAGNITUDE,
        SAME_MAGNITUDE,
        RANDOM_MAGNITUDE,

        // Input angle
        MATCH_FACING_YAW,
        ANTI_FACING_YAW,
        SAME_YAW,
        RANDOM_YAW,

        // Buttons
        SAME_BUTTONS,
        NO_BUTTONS,
        RANDOM_BUTTONS,

        // Scripts
        NO_SCRIPT,
        PBDR,
        RUN_DOWNHILL,
        REWIND
    };

    std::unordered_set<MovementOptions> GetMovementOptions(uint64_t rngHash)
    {
        std::unordered_set<MovementOptions> movementOptions;

        movementOptions.insert(ChooseMovementOption(rngHash, 
            {
                {MovementOptions::MAX_MAGNITUDE, 4},
                {MovementOptions::ZERO_MAGNITUDE, 1},
                {MovementOptions::SAME_MAGNITUDE, 1},
                {MovementOptions::RANDOM_MAGNITUDE, 2}
            }));

        movementOptions.insert(ChooseMovementOption(rngHash,
            {
                {MovementOptions::MATCH_FACING_YAW, 4},
                {MovementOptions::ANTI_FACING_YAW, 0.5},
                {MovementOptions::SAME_YAW, 1},
                {MovementOptions::RANDOM_YAW, 4}
            }));

        movementOptions.insert(ChooseMovementOption(rngHash,
            {
                {MovementOptions::SAME_BUTTONS, 1},
                {MovementOptions::NO_BUTTONS, 2},
                {MovementOptions::RANDOM_BUTTONS, 1}
            }));

        movementOptions.insert(ChooseMovementOption(rngHash,
            {
                {MovementOptions::NO_SCRIPT, 95},
                {MovementOptions::PBDR, 10},
                {MovementOptions::RUN_DOWNHILL, 20},
                {MovementOptions::REWIND, 5}
            }));

        return movementOptions;
    }

    bool ApplyMovement(std::unordered_set<MovementOptions> movementOptions, uint64_t rngHash)
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));

                // Scripts
                if (movementOptions.contains(MovementOptions::RUN_DOWNHILL))
                {
                    BitFsPyramidOscillation_ParamsDto params;
                    params.roughTargetAngle = marioState->faceAngle[1];
                    params.ignoreXzSum = true;
                    if (Modify<BitFsPyramidOscillation_RunDownhill>(params).asserted)
                        return true;
                }
                else if (movementOptions.contains(MovementOptions::PBDR) && Pbdr(rngHash))
                    return true;
                else if (movementOptions.contains(MovementOptions::REWIND))
                {
                    int64_t currentFrame = GetCurrentFrame();
                    int maxRewind = (currentFrame - config.StartFrame) / 2;
                    int rewindFrames = (rngHash % 100) * maxRewind / 100;
                    Load(currentFrame - rewindFrames);
                    return true;
                }

                // stick mag
                float intendedMag = 0;
                if (movementOptions.contains(MovementOptions::MAX_MAGNITUDE))
                    intendedMag = 32.0f;
                else if (movementOptions.contains(MovementOptions::ZERO_MAGNITUDE))
                    intendedMag = 0;
                else if (movementOptions.contains(MovementOptions::SAME_MAGNITUDE))
                    intendedMag = marioState->intendedMag;
                else if (movementOptions.contains(MovementOptions::RANDOM_MAGNITUDE))
                    intendedMag = (UpdateHashReturnPrev(rngHash) % 10000) / 32.0f;

                // Intended yaw
                int16_t intendedYaw = 0;
                if (movementOptions.contains(MovementOptions::MATCH_FACING_YAW))
                    intendedYaw = marioState->faceAngle[1];
                else if (movementOptions.contains(MovementOptions::ANTI_FACING_YAW))
                    intendedYaw = marioState->faceAngle[1] + 0x8000;
                else if (movementOptions.contains(MovementOptions::SAME_YAW))
                    intendedYaw = marioState->intendedYaw;
                else if (movementOptions.contains(MovementOptions::RANDOM_YAW))
                    intendedYaw = UpdateHashReturnPrev(rngHash);

                // Buttons
                uint16_t buttons = 0;
                if (movementOptions.contains(MovementOptions::SAME_BUTTONS))
                    buttons = GetInputs(GetCurrentFrame() - 1).buttons;
                else if (movementOptions.contains(MovementOptions::NO_BUTTONS))
                    buttons = 0;
                else if (movementOptions.contains(MovementOptions::RANDOM_BUTTONS))
                {
                    buttons |= Buttons::B & UpdateHashReturnPrev(rngHash) % 2;
                    buttons |= Buttons::Z & UpdateHashReturnPrev(rngHash) % 2;
                    buttons |= Buttons::C_UP & UpdateHashReturnPrev(rngHash) % 2;
                }
                    
                // Calculate and execute input
                auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, intendedMag, camera->yaw);
                AdvanceFrameWrite(Inputs(buttons, stick.first, stick.second));

                return true;
            }).executed;
    }

    StateBin<SShotState_BitfsDr> GetStateBin()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];
        //if (pyramid->behavior != pyramidmBehavior)

        uint64_t s = 0;
        if (marioState->action == ACT_BRAKING) s = 0;
        if (marioState->action == ACT_DIVE) s = 1;
        if (marioState->action == ACT_DIVE_SLIDE) s = 2;
        if (marioState->action == ACT_FORWARD_ROLLOUT) s = 3;
        if (marioState->action == ACT_FREEFALL_LAND_STOP) s = 4;
        if (marioState->action == ACT_FREEFALL) s = 5;
        if (marioState->action == ACT_FREEFALL_LAND) s = 6;
        if (marioState->action == ACT_TURNING_AROUND) s = 7;
        if (marioState->action == ACT_FINISH_TURNING_AROUND) s = 8;
        if (marioState->action == ACT_WALKING) s = 9;

        s *= 30;
        s += (int)((40 - marioState->vel[1]) / 4);

        float norm_regime_min = .69;
        //float norm_regime_max = .67;
        float target_xnorm = -.30725;
        float target_znorm = .3665;
        float x_delt = pyramid->oTiltingPyramidNormalX - target_xnorm;
        float z_delt = pyramid->oTiltingPyramidNormalZ - target_znorm;
        float x_remainder = x_delt * 100 - floor(x_delt * 100);
        float z_remainder = z_delt * 100 - floor(z_delt * 100);


        //if((fabs(pyraXNorm) + fabs(pyraZNorm) < norm_regime_min) ||
        //   (fabs(pyraXNorm) + fabs(pyraZNorm) > norm_regime_max)){  //coarsen for bad norm regime
        if ((x_remainder > .001 && x_remainder < .999) || (z_remainder > .001 && z_remainder < .999) ||
            (fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ) < norm_regime_min)) //coarsen for not target norm envelope
        { 
            s *= 14;
            s += (int)((pyramid->oTiltingPyramidNormalX + 1) * 7);

            s *= 14;
            s += (int)((pyramid->oTiltingPyramidNormalZ + 1) * 7);

            s += 1000000 + 1000000 * (int)floor((marioState->forwardVel + 20) / 8);
            s += 100000000 * (int)floor((float)marioState->faceAngle[1] / 16384.0);

            s *= 2;
            s += 1; //mark bad norm regime

            return SShotState_BitfsDr
            (
                (uint8_t)floor((marioState->pos[0] + 2330) / 200),
                (uint8_t)floor((marioState->pos[1] + 3200) / 400),
                (uint8_t)floor((marioState->pos[2] + 1090) / 200),
                s
            );
        }

        s *= 200;
        s += (int)((pyramid->oTiltingPyramidNormalX + 1) * 100);

        float xzSum = fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ);

        s *= 10;
        xzSum += (int)((xzSum - norm_regime_min) * 100);
        //s += (int)((pyraZNorm + 1)*100);

        s *= 30;
        s += (int)((pyramid->oTiltingPyramidNormalY - .7) * 100);

        //fifd: Hspd mapped into sections {0-1, 1-2, ...}
        s += 30000000 + 30000000 * (int)floor((marioState->forwardVel + 20));

        //fifd: Yaw mapped into sections
        //s += 100000000 * (int)floor((float)marioYawFacing / 2048.0);
        s += ((uint64_t)1200000000) * (int)floor((float)marioState->faceAngle[1] / 4096.0);

        s *= 2; //mark good norm regime

        return SShotState_BitfsDr
        (
            (uint8_t)floor((marioState->pos[0] + 2330) / 10),
            (uint8_t)floor((marioState->pos[1] + 3200) / 50),
            (uint8_t)floor((marioState->pos[2] + 1090) / 10),
            s
        );
    }

    bool ValidateBlock()
    {
        MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
        Camera* camera = *(Camera**)(resource->addr("gCamera"));
        const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        if (marioState->pos[0] < -2330) return false;
        if (marioState->pos[0] > -1550) return false;
        if (marioState->pos[2] < -1090) return false;
        if (marioState->pos[2] > -300) return false;
        if (marioState->pos[1] > -2760) return false;
        if (pyramid->oTiltingPyramidNormalZ < -.15 || pyramid->oTiltingPyramidNormalX > 0.15) return false; //stay in desired quadrant
        if (marioState->action != ACT_BRAKING && marioState->action != ACT_DIVE && marioState->action != ACT_DIVE_SLIDE &&
            marioState->action != ACT_FORWARD_ROLLOUT && marioState->action != ACT_FREEFALL_LAND_STOP && marioState->action != ACT_FREEFALL &&
            marioState->action != ACT_FREEFALL_LAND && marioState->action != ACT_TURNING_AROUND &&
            marioState->action != ACT_FINISH_TURNING_AROUND && marioState->action != ACT_WALKING) {
            return false;
        } //not useful action, such as lava boost
        if (marioState->action == ACT_FREEFALL && marioState->vel[1] > -20.0) return false;//freefall without having done nut spot chain
        if (marioState->floorHeight > -3071 && marioState->vel[1] > marioState->floorHeight + 4 &&
            marioState->vel[1] != 22.0) return false;//above pyra by over 4 units
        if (marioState->floorHeight == -3071 && marioState->action != ACT_FREEFALL) return false; //diving/dring above lava

        if (marioState->action == ACT_FORWARD_ROLLOUT && fabs(pyramid->oTiltingPyramidNormalX) > .3 && fabs(pyramid->oTiltingPyramidNormalX) + fabs(pyramid->oTiltingPyramidNormalZ) > .65 &&
            marioState->pos[0] + marioState->pos[2] > (-1945 - 715)) //make sure Mario is going toward the right/east edge
        {  
            char fileName[128];
            //printf("dr\n");
            //sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\scattershot\\x64\\Debug\\m64s\\dr\\bitfs_dr_%f_%f_%f_%f_%d.m64",
            //    pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->vel[1], omp_get_thread_num());
            //Utils::writeFile(fileName, "C:\\Users\\Tyler\\Documents\\repos\\scattershot\\x64\\Debug\\4_units_from_edge.m64", m64Diff, config.StartFrame, frame + 1);
        }

        //check on hspd > 1 confirms we're in dr land rather than quickstopping,
        //which gives the same action
        if (marioState->action == ACT_FREEFALL_LAND_STOP && marioState->pos[1] > -2980 && marioState->forwardVel > 1
            && fabs(pyramid->oTiltingPyramidNormalX) > .29 && fabs(marioState->pos[0]) > -1680)
        {
            char fileName[128];
            //printf("dr\n");
            //sprintf(fileName, "C:\\Users\\Tyler\\Documents\\repos\\scattershot\\x64\\Debug\\m64s\\drland\\bitfs_dr_%f_%f_%f_%f_%d.m64",
            //    pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->vel[1], omp_get_thread_num());
            //Utils::writeFile(fileName, "C:\\Users\\Tyler\\Documents\\repos\\scattershot\\x64\\Debug\\4_units_from_edge.m64", m64Diff, config.StartFrame, frame + 1);
        }

        return true;
    }

    float GetStateFitness()
    {
        Object* objectPool = (Object*)(resource->addr("gObjectPool"));
        Object* pyramid = &objectPool[84];

        return pyramid->oTiltingPyramidNormalY;
    }

private:
    uint64_t UpdateHashReturnPrev(uint64_t& rngHash)
    {
        uint64_t prevHash = rngHash;
        rngHash = Scattershot<TState, TResource>(rngHash);
        return prevHash;
    }

    bool Pbdr(uint64_t rngHash)
    {
        return ModifyAdhoc([&]()
            {
                MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
                Camera* camera = *(Camera**)(resource->addr("gCamera"));

                int16_t intendedYaw = marioState->faceAngle[1] + UpdateHashReturnPrev(rngHash) % 32768 - 16384;
                auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(Buttons::B | Buttons::START, stick.first, stick.second));

                if (marioState->action != ACT_DIVE_SLIDE)
                    return false;

                AdvanceFrameWrite(Inputs(0, 0, 0));
                AdvanceFrameWrite(Inputs(Buttons::START, 0, 0));
                AdvanceFrameWrite(Inputs(0, 0, 0));

                int16_t intendedYaw2 = marioState->faceAngle[1] + UpdateHashReturnPrev(rngHash) % 32768 - 16384;
                auto stick = Inputs::GetClosestInputByYawHau(intendedYaw, 32, camera->yaw);
                AdvanceFrameWrite(Inputs(Buttons::B, stick.first, stick.second));

                return true;
            }).executed;
    }
};
