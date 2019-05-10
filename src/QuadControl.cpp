#include "Common.h"
#include "QuadControl.h"

#include "Utility/SimpleConfig.h"

#include "Utility/StringUtils.h"
#include "Trajectory.h"
#include "BaseController.h"
#include "Math/Mat3x3F.h"

#ifdef __PX4_NUTTX
#include <systemlib/param/param.h>
#endif

void QuadControl::Init()
{
  BaseController::Init();

  // variables needed for integral control
  integratedAltitudeError = 0;
    
#ifndef __PX4_NUTTX
  // Load params from simulator parameter system
  ParamsHandle config = SimpleConfig::GetInstance();
   
  // Load parameters (default to 0)
  kpPosXY = config->Get(_config+".kpPosXY", 0);
  kpPosZ = config->Get(_config + ".kpPosZ", 0);
  KiPosZ = config->Get(_config + ".KiPosZ", 0);
     
  kpVelXY = config->Get(_config + ".kpVelXY", 0);
  kpVelZ = config->Get(_config + ".kpVelZ", 0);

  kpBank = config->Get(_config + ".kpBank", 0);
  kpYaw = config->Get(_config + ".kpYaw", 0);

  kpPQR = config->Get(_config + ".kpPQR", V3F());

  maxDescentRate = config->Get(_config + ".maxDescentRate", 100);
  maxAscentRate = config->Get(_config + ".maxAscentRate", 100);
  maxSpeedXY = config->Get(_config + ".maxSpeedXY", 100);
  maxAccelXY = config->Get(_config + ".maxHorizAccel", 100);

  maxTiltAngle = config->Get(_config + ".maxTiltAngle", 100);

  minMotorThrust = config->Get(_config + ".minMotorThrust", 0);
  maxMotorThrust = config->Get(_config + ".maxMotorThrust", 100);
#else
  // load params from PX4 parameter system
  //TODO
  param_get(param_find("MC_PITCH_P"), &Kp_bank);
  param_get(param_find("MC_YAW_P"), &Kp_yaw);
#endif
}

VehicleCommand QuadControl::GenerateMotorCommands(float collThrustCmd, V3F momentCmd)
{
  // Convert a desired 3-axis moment and collective thrust command to 
  //   individual motor thrust commands
  // INPUTS: 
  //   collThrustCmd: desired collective thrust [N]
  //   momentCmd: desired rotation moment about each axis [N m]
  // OUTPUT:
  //   set class member variable cmd (class variable for graphing) where
  //   cmd.desiredThrustsN[0..3]: motor commands, in [N]

  // HINTS: 
  // - you can access parts of momentCmd via e.g. momentCmd.x
  // You'll need the arm length parameter L, and the drag/thrust ratio kappa

  ////////////////////////////// BEGIN BASTI CODE ///////////////////////////

  const float l = L / sqrtf(2.f);

  const float f_1 = 0.25f * ( collThrustCmd
                            + momentCmd[0] / l
                            + momentCmd[1] / l
                            + momentCmd[2] / -this->kappa );
  const float f_2 = 0.25f * ( collThrustCmd
                            - momentCmd[0] / l
                            + momentCmd[1] / l
                            - momentCmd[2] / -this->kappa );
  const float f_3 = 0.25f * ( collThrustCmd
                            + momentCmd[0] / l
                            - momentCmd[1] / l
                            - momentCmd[2] / -this->kappa );
  const float f_4 = 0.25f * ( collThrustCmd
                            - momentCmd[0] / l
                            - momentCmd[1] / l
                            + momentCmd[2] / -this->kappa );

  cmd.desiredThrustsN[0] = CONSTRAIN(f_1, this->minMotorThrust, this->maxMotorThrust);
  cmd.desiredThrustsN[1] = CONSTRAIN(f_2, this->minMotorThrust, this->maxMotorThrust);
  cmd.desiredThrustsN[2] = CONSTRAIN(f_3, this->minMotorThrust, this->maxMotorThrust);
  cmd.desiredThrustsN[3] = CONSTRAIN(f_4, this->minMotorThrust, this->maxMotorThrust);

  /////////////////////////////// END BASTI CODE ////////////////////////////

  return cmd;
}

V3F QuadControl::BodyRateControl(V3F pqrCmd, V3F pqr)
{
  // Calculate a desired 3-axis moment given a desired and current body rate
  // INPUTS: 
  //   pqrCmd: desired body rates [rad/s]
  //   pqr: current or estimated body rates [rad/s]
  // OUTPUT:
  //   return a V3F containing the desired moments for each of the 3 axes

  // HINTS: 
  //  - you can use V3Fs just like scalars: V3F a(1,1,1), b(2,3,4), c; c=a-b;
  //  - you'll need parameters for moments of inertia Ixx, Iyy, Izz
  //  - you'll also need the gain parameter kpPQR (it's a V3F)

  ////////////////////////////// BEGIN BASTI CODE ///////////////////////////

  static V3F c1 = this->kpPQR * V3F(this->Ixx, this->Iyy, this->Izz);

  const V3F rate_error = pqrCmd - pqr;

  V3F momentCmd = c1 * rate_error;

  /////////////////////////////// END BASTI CODE ////////////////////////////

  return momentCmd;
}

// returns a desired roll and pitch rate 
V3F QuadControl::RollPitchControl(V3F accelCmd, Quaternion<float> attitude, float collThrustCmd)
{
  // Calculate a desired pitch and roll angle rates based on a desired global
  //   lateral acceleration, the current attitude of the quad, and desired
  //   collective thrust command
  // INPUTS: 
  //   accelCmd: desired acceleration in global XY coordinates [m/s2]
  //   attitude: current or estimated attitude of the vehicle
  //   collThrustCmd: desired collective thrust of the quad [N]
  // OUTPUT:
  //   return a V3F containing the desired pitch and roll rates. The Z
  //     element of the V3F should be left at its default value (0)

  // HINTS: 
  //  - we already provide rotation matrix R: to get element R[1,2] (python) use R(1,2) (C++)
  //  - you'll need the roll/pitch gain kpBank
  //  - collThrustCmd is a force in Newtons! You'll likely want to convert it to acceleration first

  V3F pqrCmd;

  ////////////////////////////// BEGIN BASTI CODE ///////////////////////////

  if ( collThrustCmd > 0 ) {
    Mat3x3F R = attitude.RotationMatrix_IwrtB();

    const float c = - collThrustCmd / this->mass;
    const float b_x_cmd = CONSTRAIN(accelCmd.x / c, -this->maxTiltAngle, this->maxTiltAngle);
    const float b_x_err = b_x_cmd - R(0,2);
    const float b_x_p_term = this->kpBank * b_x_err;

    const float b_y_cmd = CONSTRAIN(accelCmd.y / c, -this->maxTiltAngle, this->maxTiltAngle);
    const float b_y_err = b_y_cmd - R(1,2);
    const float b_y_p_term = this->kpBank * b_y_err;

    pqrCmd.x = (R(1,0) * b_x_p_term - R(0,0) * b_y_p_term) / R(2,2);
    pqrCmd.y = (R(1,1) * b_x_p_term - R(0,1) * b_y_p_term) / R(2,2);
  }
  else {
    pqrCmd.x = 0.0;
    pqrCmd.y = 0.0;
  }

  pqrCmd.z = 0;

  /////////////////////////////// END BASTI CODE ////////////////////////////

  return pqrCmd;
}

float QuadControl::AltitudeControl(float posZCmd, float velZCmd, float posZ, float velZ, Quaternion<float> attitude,
    float accelZCmd, float dt)
{
  // Calculate desired quad thrust based on altitude setpoint, actual altitude,
  //   vertical velocity setpoint, actual vertical velocity, and a vertical 
  //   acceleration feed-forward command
  // INPUTS: 
  //   posZCmd, velZCmd: desired vertical position and velocity in NED [m]
  //   posZ, velZ: current vertical position and velocity in NED [m]
  //   accelZCmd: feed-forward vertical acceleration in NED [m/s2]
  //   dt: the time step of the measurements [seconds]
  // OUTPUT:
  //   return a collective thrust command in [N]

  // HINTS: 
  //  - we already provide rotation matrix R: to get element R[1,2] (python) use R(1,2) (C++)
  //  - you'll need the gain parameters kpPosZ and kpVelZ
  //  - maxAscentRate and maxDescentRate are maximum vertical speeds. Note they're both >=0!
  //  - make sure to return a force, not an acceleration
  //  - remember that for an upright quad in NED, thrust should be HIGHER if the desired Z acceleration is LOWER

  Mat3x3F R = attitude.RotationMatrix_IwrtB();

  ////////////////////////////// BEGIN BASTI CODE ///////////////////////////


  if ( velZCmd < -this->maxAscentRate) {
    printf("%s: velZCmd < -this->maxAscentRate\n", __FUNCTION__);
    velZCmd = CONSTRAIN(velZCmd, -this->maxAscentRate, this->maxDescentRate);
  }
  else if ( velZCmd > this->maxDescentRate) {
    printf("%s: velZCmd > this->maxDescentRate\n", __FUNCTION__);
    velZCmd = CONSTRAIN(velZCmd, -this->maxAscentRate, this->maxDescentRate);
  }
  if ( velZ < -this->maxAscentRate) {
    printf("%s: acc_bar < -this->maxAscentRate\n", __FUNCTION__);
  }
  else if ( velZ > this->maxDescentRate) {
    printf("%s: acc_bar > this->maxDescentRate\n", __FUNCTION__);
  }

  const float z_err = posZCmd - posZ;
  const float z_dot_err = velZCmd - velZ;
  this->integratedAltitudeError += z_err * dt;

  const float p_term = this->kpPosZ * z_err;
  const float d_term = this->kpVelZ * z_dot_err;
  const float i_term = this->KiPosZ * this->integratedAltitudeError;

  const float acc_bar =  p_term + d_term + i_term + accelZCmd;
  //const float acc_bar =  CONSTRAIN(p_term + d_term + i_term + accelZCmd, -this->maxAscentRate, this->maxDescentRate);

  const float acc = ( CONST_GRAVITY - acc_bar ) / R(2,2);

  const float thrust = this->mass * acc;

  /////////////////////////////// END BASTI CODE ////////////////////////////
  
  return thrust;
}

// returns a desired acceleration in global frame
V3F QuadControl::LateralPositionControl(V3F posCmd, V3F velCmd, V3F pos, V3F vel, V3F accelCmdFF)
{
  // Calculate a desired horizontal acceleration based on 
  //  desired lateral position/velocity/acceleration and current pose
  // INPUTS: 
  //   posCmd: desired position, in NED [m]
  //   velCmd: desired velocity, in NED [m/s]
  //   pos: current position, NED [m]
  //   vel: current velocity, NED [m/s]
  //   accelCmdFF: feed-forward acceleration, NED [m/s2]
  // OUTPUT:
  //   return a V3F with desired horizontal accelerations. 
  //     the Z component should be 0
  // HINTS: 
  //  - use the gain parameters kpPosXY and kpVelXY
  //  - make sure you limit the maximum horizontal velocity and acceleration
  //    to maxSpeedXY and maxAccelXY

  // make sure we don't have any incoming z-component
  accelCmdFF.z = 0;
  velCmd.z = 0;
  posCmd.z = pos.z;

  // we initialize the returned desired acceleration to the feed-forward value.
  // Make sure to _add_, not simply replace, the result of your controller
  // to this variable

  ////////////////////////////// BEGIN BASTI CODE ///////////////////////////

  const V3F kpPos(this->kpPosXY, this->kpPosXY, 0.f);
  const V3F kpVel(this->kpVelXY, this->kpVelXY, 0.f);
  
  const float scalarSpeedXy = CONSTRAIN(velCmd.mag(), 0.f, this->maxSpeedXY);
  const V3F velCmdConstrained = velCmd.norm() * scalarSpeedXy;

  const V3F accelCmd = kpPos * ( posCmd - pos ) + kpVel * ( velCmdConstrained - vel ) + accelCmdFF;

  const float scalarAccelCmd = CONSTRAIN(accelCmd.mag(), 0.f, this->maxAccelXY);
  const V3F accelCmdConstrained = accelCmd.norm() * scalarAccelCmd;

  /////////////////////////////// END BASTI CODE ////////////////////////////

  return accelCmdConstrained;
}

// returns desired yaw rate
float QuadControl::YawControl(float yawCmd, float yaw)
{
  // Calculate a desired yaw rate to control yaw to yawCmd
  // INPUTS: 
  //   yawCmd: commanded yaw [rad]
  //   yaw: current yaw [rad]
  // OUTPUT:
  //   return a desired yaw rate [rad/s]
  // HINTS: 
  //  - use fmodf(foo,b) to unwrap a radian angle measure float foo to range [0,b]. 
  //  - use the yaw control gain parameter kpYaw

  ////////////////////////////// BEGIN BASTI CODE ///////////////////////////

  float yaw_cmd_2_pi = 0;
  if ( yawCmd > 0 ) {
    yaw_cmd_2_pi = fmodf(yawCmd, 2 * F_PI);
  }
  else {
    yaw_cmd_2_pi = -fmodf(-yawCmd, 2 * F_PI);
  }

  float err = yaw_cmd_2_pi - yaw;

  while ( err > F_PI ) {
    err -= 2 * F_PI;
  }
  while ( err < -F_PI ) {
    err += 2 * F_PI;
  }

  const float yawRateCmd = this->kpYaw * err;

  /////////////////////////////// END BASTI CODE ////////////////////////////

  return yawRateCmd;

}

VehicleCommand QuadControl::RunControl(float dt, float simTime)
{
  curTrajPoint = GetNextTrajectoryPoint(simTime);

  float collThrustCmd = AltitudeControl(curTrajPoint.position.z, curTrajPoint.velocity.z, estPos.z, estVel.z, estAtt, curTrajPoint.accel.z, dt);

  // reserve some thrust margin for angle control
  float thrustMargin = .1f*(maxMotorThrust - minMotorThrust);
  collThrustCmd = CONSTRAIN(collThrustCmd, (minMotorThrust+ thrustMargin)*4.f, (maxMotorThrust-thrustMargin)*4.f);
  
  V3F desAcc = LateralPositionControl(curTrajPoint.position, curTrajPoint.velocity, estPos, estVel, curTrajPoint.accel);
  
  V3F desOmega = RollPitchControl(desAcc, estAtt, collThrustCmd);
  desOmega.z = YawControl(curTrajPoint.attitude.Yaw(), estAtt.Yaw());

  V3F desMoment = BodyRateControl(desOmega, estOmega);

  return GenerateMotorCommands(collThrustCmd, desMoment);
}
