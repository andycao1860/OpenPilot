/*
 ******************************************************************************
 *
 * @file       grounddrivecontroller.cpp
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2015.
 * @brief      Ground drive controller
 *		the required PathDesired LAND mode.
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

extern "C" {
#include <openpilot.h>

#include <callbackinfo.h>

#include <math.h>
#include <pid.h>
#include <CoordinateConversions.h>
#include <sin_lookup.h>
#include <pathdesired.h>
#include <paths.h>
#include "plans.h"
#include <sanitycheck.h>

#include <homelocation.h>
#include <accelstate.h>
#include <groundpathfollowersettings.h>
#include <flightstatus.h>
#include <flightmodesettings.h>
#include <pathstatus.h>
#include <positionstate.h>
#include <velocitystate.h>
#include <velocitydesired.h>
#include <stabilizationdesired.h>
#include <airspeedstate.h>
#include <attitudestate.h>
#include <takeofflocation.h>
#include <poilocation.h>
#include <manualcontrolcommand.h>
#include <systemsettings.h>
#include <stabilizationbank.h>
#include <stabilizationdesired.h>
#include <pathsummary.h>
}

// C++ includes
#include "grounddrivecontroller.h"

// Private constants

// pointer to a singleton instance
GroundDriveController *GroundDriveController::p_inst = 0;

GroundDriveController::GroundDriveController()
    : groundSettings(0), mActive(false)
{}

// Called when mode first engaged
void GroundDriveController::Activate(void)
{
    if (!mActive) {
        mActive = true;
        SettingsUpdated();
        controlNE.Activate();
        resetGlobals();
        mMode   = pathDesired->Mode;
    }
}

uint8_t GroundDriveController::IsActive(void)
{
    return mActive;
}

uint8_t GroundDriveController::Mode(void)
{
    return mMode;
}

// Objective updated in pathdesired
void GroundDriveController::ObjectiveUpdated(void)
{}

void GroundDriveController::Deactivate(void)
{
    if (mActive) {
        mActive = false;
        resetGlobals();
        controlNE.Deactivate();
    }
}


void GroundDriveController::SettingsUpdated(void)
{
    const float dT = groundSettings->UpdatePeriod / 1000.0f;
    controlNE.UpdatePositionalParameters(groundSettings->HorizontalPosP);
    controlNE.UpdateParameters(groundSettings->SpeedPI.Kp,
                               groundSettings->SpeedPI.Ki,
                               groundSettings->SpeedPI.Kd,
                               groundSettings->SpeedPI.Beta,
                               dT,
                               groundSettings->HorizontalVelMax);


      controlNE.UpdateCommandParameters(groundSettings->ThrustLimit.Min, groundSettings->ThrustLimit.Max, groundSettings->VelocityFeedForward);
}

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t GroundDriveController::Initialize(GroundPathFollowerSettingsData *ptr_groundSettings)
{
    PIOS_Assert(ptr_groundSettings);

    groundSettings = ptr_groundSettings;

    resetGlobals();

    return 0;
}

/**
 * reset integrals
 */
void GroundDriveController::resetGlobals()
{
    pathStatus->path_time = 0.0f;
}

void GroundDriveController::UpdateAutoPilot()
{
    uint8_t result = updateAutoPilotGround();

    if (result) {
        AlarmsSet(SYSTEMALARMS_ALARM_GUIDANCE, SYSTEMALARMS_ALARM_OK);
    } else {
        pathStatus->Status = PATHSTATUS_STATUS_CRITICAL;
        AlarmsSet(SYSTEMALARMS_ALARM_GUIDANCE, SYSTEMALARMS_ALARM_WARNING);
    }

    PathStatusSet(pathStatus);
}

/**
 * fixed wing autopilot:
 * straight forward:
 * 1. update path velocity for limited motion crafts
 * 2. update attitude according to default fixed wing pathfollower algorithm
 */
uint8_t GroundDriveController::updateAutoPilotGround()
{
    updatePathVelocity(groundSettings->CourseFeedForward);
    return updateGroundDesiredAttitude();
}

/**
 * Compute desired velocity from the current position and path
 */
void GroundDriveController::updatePathVelocity(float kFF)
{
    PositionStateData positionState;

    PositionStateGet(&positionState);
    VelocityStateData velocityState;
    VelocityStateGet(&velocityState);
    VelocityDesiredData velocityDesired;
    controlNE.UpdateVelocityState(velocityState.North, velocityState.East);

    // look ahead kFF seconds
    float cur[3]   = { positionState.North + (velocityState.North * kFF),
                       positionState.East + (velocityState.East * kFF),
                       positionState.Down + (velocityState.Down * kFF) };
    struct path_status progress;
    path_progress(pathDesired, cur, &progress, false);

    controlNE.ControlPositionWithPath(&progress);
    float north, east;
    controlNE.GetVelocityDesired(&north, &east);
    velocityDesired.North = north;
    velocityDesired.East  = east;
    velocityDesired.Down  = 0.0f;

#if 0
    if (limited &&
        // if a plane is crossing its desired flightpath facing the wrong way (away from flight direction)
        // it would turn towards the flightpath to get on its desired course. This however would reverse the correction vector
        // once it crosses the flightpath again, which would make it again turn towards the flightpath (but away from its desired heading)
        // leading to an S-shape snake course the wrong way
        // this only happens especially if HorizontalPosP is too high, as otherwise the angle between velocity desired and path_direction won't
        // turn steep unless there is enough space complete the turn before crossing the flightpath
        // in this case the plane effectively needs to be turned around
        // indicators:
        // difference between correction_direction and velocitystate >90 degrees and
        // difference between path_direction and velocitystate >90 degrees  ( 4th sector, facing away from everything )
        // fix: ignore correction, steer in path direction until the situation has become better (condition doesn't apply anymore)
        // calculating angles < 90 degrees through dot products
        (vector_lengthf(progress.path_vector, 2) > 1e-6f) &&
        ((progress.path_vector[0] * velocityState.North + progress.path_vector[1] * velocityState.East) < 0.0f) &&
        ((progress.correction_vector[0] * velocityState.North + progress.correction_vector[1] * velocityState.East) < 0.0f)) {
        ;

    }
#endif


    // update pathstatus
    pathStatus->error     = progress.error;
    pathStatus->fractional_progress  = progress.fractional_progress;
    pathStatus->path_direction_north = progress.path_vector[0];
    pathStatus->path_direction_east  = progress.path_vector[1];
    pathStatus->path_direction_down  = progress.path_vector[2];

    pathStatus->correction_direction_north = progress.correction_vector[0];
    pathStatus->correction_direction_east  = progress.correction_vector[1];
    pathStatus->correction_direction_down  = progress.correction_vector[2];

    VelocityDesiredSet(&velocityDesired);
}

/**
 * Compute desired attitude for ground vehicles
 */
uint8_t GroundDriveController::updateGroundDesiredAttitude()
{
    float northCommand, eastCommand;
    controlNE.GetNECommand(&northCommand, &eastCommand);

    float courseCommand;
    float speedCommand;
    courseCommand      = RAD2DEG(atan2f(eastCommand, northCommand));
    speedCommand       = sqrtf( squaref(northCommand) + squaref(eastCommand));

    StabilizationDesiredData stabDesired;
    stabDesired.Roll   = 0.0f;
    stabDesired.Pitch  = 0.0f;
    stabDesired.Yaw    = courseCommand;

    stabDesired.Thrust = boundf(speedCommand, groundSettings->ThrustLimit.Min, groundSettings->ThrustLimit.Max);

    stabDesired.StabilizationMode.Roll   = STABILIZATIONDESIRED_STABILIZATIONMODE_MANUAL;
    stabDesired.StabilizationMode.Pitch  = STABILIZATIONDESIRED_STABILIZATIONMODE_MANUAL;
    stabDesired.StabilizationMode.Yaw    = STABILIZATIONDESIRED_STABILIZATIONMODE_MANUAL;
    stabDesired.StabilizationMode.Thrust = STABILIZATIONDESIRED_STABILIZATIONMODE_MANUAL;
    StabilizationDesiredSet(&stabDesired);

    return 1;
}