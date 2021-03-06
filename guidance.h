#ifndef guidance_h
#define guidance_h

#include "global.h"
#include "Computer_input_handler.h"


enum boat_states
{
	close_hauled_wind_from_left, //0
	close_hauled_wind_from_right,//1
	tacking_going_from_wind_from_left_to_right, //2
	tacking_going_from_wind_from_right_to_left, //3
	generel_direction_wind_from_right, //4
	generel_direction_wind_from_left, //5
	jibe_going_from_wind_from_left_to_right, //6
	jibe_going_from_wind_from_right_to_left, //7
	down_wind_wind_from_right, // 8
	down_wind_wind_from_left, // 9
};


class Navigation_guidance{ //this class contains all the previous groups work on navigating the boat, with mayor changes to suit the way i have made the program.
private:
	boat_states state = close_hauled_wind_from_left;
	boat_states next_state;
	float bearing_to_target; //compass bearing to target
	float bearing_to_target_relative_to_wind; //if 35, target is 35 degrees to the right of the wind
	float distance_to_target; //distance in m
	float frozen_tacking_direction;
	bool has_boat_been_at_tacking_target;
	long int time_stamp_for_tacking;
	Location target_location;
	
	float real_part_of_complex_from_old_code = 0;
	float imaginary_part_of_complex_from_old_code = 0;



public:
	Servo Rudder_Servo;
	Servo sail_servo;
	Servo front_sail_servo;


	float theta_A;
	float theta_B;
	float theta_AB;
	float theta_BA;
	float x;
	bool collision_avoidance_active = false;
	bool collision_avoidance_did_evasion = false;


#define INTEGRATOR_MAX 20 // [degrees], influence of the integrator
#define RATEOFTURN_MAX 36 // [degrees/second]
#define GAIN_P -1 //PID Controller Setting
#define GAIN_I 0 //PID Controller Setting
#define TACKING_ZONE 55
#define SAIL_CONTROL_ZERO_POINT 45
#define IN_RANGE_DISTANCE 6
#define DOWN_WIND_ZONE 135
#define TACKING_TURNING_VALUE 20
#define TACKING_ZONE_DISTANCE 20
#define TACKING_ZONE_WIDE_ANGLE (TACKING_ZONE / 2)
#define TACKING_ZONE_NARROW_ANGLE 5
#define TOLERANCE_FOR_COMPLETED_TACKING 15
#define TIMEOUT_FOR_COMPLETED_TACKING 8000
#define RUDDER_LIMIT 35 
#define COLLISION_AVOIDANCE_INNER_LIMIT 10
#define COLLISION_AVOIDANCE_OUTER_LIMIT 15
#define MAX_ALLOWED_ROLL_BEFORE_CORRECTION 30


	


	void guidance()
	{
		handle_target(); //checks if we are at the target, if we are, go to next
		get_target_info();//calculate distance to target beraing to target and so on
		next_state_logic();
		state = next_state; //this is here to ensure that collision avoidance can "avoid" this thing. 
		determine_path_bearing(); //determines which way to go, dependent on which state we are in.
		do_colission_avoidance(); //may overwrite the next state logic
		
		sail_control();
		send_data_to_data_logging();
		if (SIMULATOR_MODE && SIMULATOR_MODE_MOVE_AUTOMATICALLY) {
			global.global_wind_bearing = 0;

			global.bearing_container.compass_bearing = global.desired_heading; //turn to the way we want to go
			global.wind_bearing = (int)(global.bearing_container.compass_bearing - global.global_wind_bearing) % 360;
			if (global.wind_bearing > 180) global.wind_bearing -= 360;
			simulator_mode_move_forward(); //move forward
			//Serial.println(global.wind_bearing);
		}
	}

	void do_colission_avoidance(void){ //TODO add debug messages to this
		//first determine if we should do collision avoidance
		int do_avoidance = -1; //this marks which boat we avoid, its implemented as only 1 boat for now.
		bool forced = false; //this determine wheter we avoid no mater what, this is the case if we are very close to the other boat.
		int minimum_distance_to_other_boat = COLLISION_AVOIDANCE_OUTER_LIMIT;
		for (int i = 0; i < NUMBER_OF_OTHER_BOATS_IN_BOAT_ARRAY; i++)
		{

			if (global.gps_data.location.distance_to(global.other_boats[i]) < minimum_distance_to_other_boat) {
				minimum_distance_to_other_boat = global.gps_data.location.distance_to(global.other_boats[i]);
				do_avoidance = i;
			}
		}

		// HARD COLLISION AVOIDANCE 
		collision_avoidance_active = false;
		collision_avoidance_did_evasion = false;
		if (do_avoidance != -1) {
			collision_avoidance_active = true;
			collision_avoidance_did_evasion = true;
			theta_A = global.bearing_container.compass_bearing - global.global_wind_bearing;//Boat A heading direction refers to winding direction
			if (theta_A > 180) theta_A -= 360;
			if (theta_A < -180) theta_A += 360;
			theta_B = global.other_boats[do_avoidance].bearing - global.global_wind_bearing;//Boat B heading direction refers to winding direction //TODO change to boat b
			if (theta_B > 180) theta_B -= 360;
			if (theta_B < -180) theta_B += 360;
			theta_AB = global.gps_data.location.bearing_to(global.other_boats[do_avoidance]) - global.global_wind_bearing;//theta_AB is the angle between wind direction and line A-B(draw straight line from boat A to boat B )
			if (theta_AB > 180) theta_AB -= 360;
			if (theta_AB < -180) theta_AB += 360;
			theta_BA = theta_AB - 180;//theta _BA is the angle between wind direction and Line B-A(draw straight line from boat B to boat A )
			if (theta_BA < -180) theta_BA += 360; //dont know why i need to flip the sign here, but according to datalog i do.
			if (theta_BA > 180) theta_BA -= 360;
			
			float v_A = global.gps_data.location.speed;//the velocity of Boat A
			float v_B = global.other_boats[do_avoidance].speed;//the velocity of Boat B
			x = global.gps_data.location.distance_to(global.other_boats[do_avoidance]);//is the distance between boat A and boat B
			float theta_rub = int(global.global_wind_bearing + TACKING_ZONE) % 360;
			float theta_lub = int(global.global_wind_bearing - TACKING_ZONE) % 360;
			float theta_rdb = int(global.global_wind_bearing + DOWN_WIND_ZONE) % 360;
			float theta_ldb = int(global.global_wind_bearing - DOWN_WIND_ZONE) % 360;


			if (x < 10)
			{// meters
				if (theta_AB >= 0)
				{
					global.desired_heading = (int)(global.global_wind_bearing - 90) % 360;  // degrees 
				}
				else
				{
					global.desired_heading = (int)(global.global_wind_bearing + 90) % 360;
				}
			}

			// SOFT COLLISION AVOIDANCE

			else if (10 <= x && x < 20)
			{
				if (theta_A*theta_B < 0)// meeting/cross-path
				{
					if (theta_A > 0)
					{
						if (-180 < theta_B && theta_B < theta_BA)
						{
							if (theta_AB - 30 < theta_rub)//the right up barrier to determine if there is need to tack
							{
								do_tack_for_collision_avoidance();
							}
							else
							{
								global.desired_heading = (int)(theta_AB - 30)%360;
							}
						}
						else if (theta_BA < theta_B && theta_B < 180)
						{
							if (theta_AB + 30 > theta_rdb)//the right down barrier to determine if there is need to jibe
							{
								do_jibe_for_collision_avoidance();
							}
							else
							{
								global.desired_heading = (int)(theta_AB + 30)%360;
							}
						}
						else
						{
							//Do nothing theta_A = theta_A;
							collision_avoidance_did_evasion = false;
						}

					}
					else // theta_A < 0
					{
						// do nothing theta_A = theta_A;
						collision_avoidance_did_evasion = false;
					}
				}
				else if (-90 < theta_AB && theta_AB < 90) // theta_A*theta_B > 0
				{
					// do nothing theta_A = theta_A;
					collision_avoidance_did_evasion = false;
				}
				else
				{
					if ((theta_A - theta_AB)*(theta_B - theta_AB) < 0)
					{
						// do nothing theta_A = theta_A;
						collision_avoidance_did_evasion = false;
					}
					else
					{
						if (theta_AB > 0)
						{
							if (theta_A > 0)
							{
								if (v_A < v_B)// speeds of the boats
								{
									// do nothing theta_A = theta_A;
									collision_avoidance_did_evasion = false;
								}
								else
								{
									if (theta_B - 30 < theta_rub)
									{
										do_tack_for_collision_avoidance();
									}
									else
									{
										global.desired_heading = (int)(theta_B - 30)%360;
									}
								}
							}
							else//theta_A<0
							{
								if (v_A > v_B)
								{
									// do nothing theta_A = theta_A;
									collision_avoidance_did_evasion = false;
								}
								else
								{
									if (theta_B + 30 > theta_lub)//the left up barrier to determine if there is need to tack
									{
										do_tack_for_collision_avoidance();
									}
									else
									{
										global.desired_heading = (int)(theta_B + 30)%360;
									}
								}
							}
						}
						else // theta_AB < 0
						{
							if (theta_A < 0)
							{
								if (v_A < v_B)
								{
									// do nothing theta_A = theta_A;
									collision_avoidance_did_evasion = false;
								}
								else
								{
									if (theta_B + 30 > theta_lub)//the left up barrier to determine if there is need to tack
									{
										do_tack_for_collision_avoidance();
									}
									else
									{
										global.desired_heading = (int)(theta_B + 30) % 360;
									}
								}
							}
							else//theta_A>0
							{
								if (v_A > v_B)
								{
									// do nothing theta_A = theta_A;
									collision_avoidance_did_evasion = false;
								}
								else
								{
									if (theta_B + 30 < theta_rub)//the right up barrier to determine if there is need to tack
									{
										do_tack_for_collision_avoidance();
									}
									else
									{
										global.desired_heading = (int)(theta_B + 30)%360;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	void do_tack_for_collision_avoidance(void){
		if (theta_AB > 0) { //the other boat is to the right of the wind
			next_state = tacking_going_from_wind_from_left_to_right;
		}
		else {
			next_state = tacking_going_from_wind_from_right_to_left;
		}
		determine_path_bearing();//TODO determine wheter this is a good idea
	
	}
	void do_jibe_for_collision_avoidance(void){
		if (theta_AB > 0) { //the other boat is to the right of the wind
			next_state = jibe_going_from_wind_from_left_to_right;
		}
		else {
			next_state = jibe_going_from_wind_from_right_to_left;
		}
		determine_path_bearing(); //TODO determine wheter this is a good idea
	}
	void send_data_to_data_logging(void){
		/*	
	double Boat1_Data_X_T_b_real;
	double Boat1_Data_X_T_b_imag;
	double global_Rudder_Desired_Angle;
	int waypoints_count;
	float desired_heading;
	float distance_to_target;
	float bearing_to_target;
	float current_state;*/

		global.data_from_navigation_to_log.Boat1_Data_X_T_b_real = real_part_of_complex_from_old_code;
		global.data_from_navigation_to_log.Boat1_Data_X_T_b_imag = imaginary_part_of_complex_from_old_code;
		global.data_from_navigation_to_log.global_Rudder_Desired_Angle = global.Rudder_Desired_Angle;
		global.data_from_navigation_to_log.waypoints_count = global.waypoints.actual_size;
		global.data_from_navigation_to_log.desired_heading = global.desired_heading;
		global.data_from_navigation_to_log.distance_to_target = distance_to_target;
		global.data_from_navigation_to_log.bearing_to_target = bearing_to_target;
		global.data_from_navigation_to_log.bearing_to_target_relative_to_wind = bearing_to_target_relative_to_wind;
		global.data_from_navigation_to_log.current_state = state;
		global.data_from_navigation_to_log.theta_A = theta_B;
		global.data_from_navigation_to_log.theta_B = theta_B;
		global.data_from_navigation_to_log.theta_AB = theta_AB;
		global.data_from_navigation_to_log.theta_BA = theta_BA;
		global.data_from_navigation_to_log.x = x;
		global.data_from_navigation_to_log.collision_avoidance_active = collision_avoidance_active;
		global.data_from_navigation_to_log.collision_avoidance_did_evasion = collision_avoidance_did_evasion;

		/*
			float theta_A;
	float theta_B;
	float theta_AB;
	float theta_BA;
	float x;
	bool collision_avoidance_active = false;
	bool collision_avoidance_did_evasion = false;
	*/

			/*		distance_to_target = global.gps_data.location.distance_to(target_location);
		bearing_to_target = global.gps_data.location.bearing_to(target_location);
		//bearing_to_target_relative_to_wind = fmod(bearing_to_target - global.global_wind_bearing, 360) - 180; //ensure the range is from -180  to +180
		bearing_to_target_relative_to_wind = ((int)((bearing_to_target - global.global_wind_bearing))) % 360;
	
		if (bearing_to_target_relative_to_wind > 180) bearing_to_target_relative_to_wind -= 360;
		if (bearing_to_target_relative_to_wind < -180) bearing_to_target_relative_to_wind += 360;
		
		real_part_of_complex_from_old_code = sin(bearing_to_target_relative_to_wind*PI / 180)*distance_to_target;
		imaginary_part_of_complex_from_old_code = cos(bearing_to_target_relative_to_wind*PI / 180)*distance_to_target;
*/
	}
	void sail_control(void){
		int sail_control_value = global.desired_heading - global.global_wind_bearing;// SAIL_CONTROL_ZERO_POINT;
		if (sail_control_value < 0) sail_control_value *= -1;
		if (sail_control_value > 180){
			sail_control_value *= -1;
			sail_control_value += 360;
		}
		
		if (sail_control_value < 55) sail_control_value = 0;
		else sail_control_value -= 55; 

		sail_control_value *= 2.25;
		sail_control_value *= -1; //inverting
		sail_control_value += 180; //inverting
		if (global.bearing_container.roll > MAX_ALLOWED_ROLL_BEFORE_CORRECTION || global.bearing_container.roll < -MAX_ALLOWED_ROLL_BEFORE_CORRECTION){
			sail_control_value = 0;
		}
		sail_servo.write(sail_control_value);

		if (global.debug_handler.path_finding_debug) {
			Serial.print("main sail: ");
			Serial.print(sail_control_value);
			Serial.println("");


		}
		if (global.wind_bearing > 0){
			front_sail_servo.write(0);
			if(global.debug_handler.path_finding_debug) Serial.println("wind from right");
		}
		else {
			front_sail_servo.write(180);
			if (global.debug_handler.path_finding_debug) Serial.println("wind from left");
		}
		
	}
	 public:
	void guidance_start()
	{
		//test comment
		if (global.waypoints.actual_size== 0) { //there is no targets
			target_location.latitude = 55;
			target_location.longtitude = 9; //we default to somewhere
			Serial.println("no waypoints avaliable, defaulted to build in guidance target");
		}
		else {
			target_location = global.waypoints.dequeue();
			Serial.println("got start target from waypoint queue");
		}

		distance_to_target = global.gps_data.location.distance_to(target_location);
		bearing_to_target = global.gps_data.location.bearing_to(target_location);
		bearing_to_target_relative_to_wind = ((int)((bearing_to_target - global.global_wind_bearing)))%360;

		if (bearing_to_target_relative_to_wind > 0){ //target is right of wind
			if (bearing_to_target_relative_to_wind > TACKING_ZONE) {
				state = generel_direction_wind_from_left;
				next_state = state;
			}
			else {
				state = close_hauled_wind_from_left;
				next_state = state;
			}
		}
		else { //target is left of wind
			if (bearing_to_target_relative_to_wind < -TACKING_ZONE){
				state = generel_direction_wind_from_right;
				next_state = state;
			}
			else {
				state = close_hauled_wind_from_right;
				next_state = state;
			}
		}
	}

	void handle_target(void){
		if (global.gps_data.location.distance_to(target_location) < IN_RANGE_DISTANCE || global.force_load_waypoint_in_guidance){ 
			if (global.waypoints.actual_size> 0){ //there are still waypoints to take
				target_location = global.waypoints.dequeue();
				global.force_load_waypoint_in_guidance = false;
			}
			else{
				//TODO print message here
			}
		}
	}

	void get_target_info(void){

		distance_to_target = global.gps_data.location.distance_to(target_location);
		bearing_to_target = global.gps_data.location.bearing_to(target_location);
		//bearing_to_target_relative_to_wind = fmod(bearing_to_target - global.global_wind_bearing, 360) - 180; //ensure the range is from -180  to +180
		bearing_to_target_relative_to_wind = ((int)((bearing_to_target - global.global_wind_bearing))) % 360;
	
		if (bearing_to_target_relative_to_wind > 180) bearing_to_target_relative_to_wind -= 360;
		if (bearing_to_target_relative_to_wind < -180) bearing_to_target_relative_to_wind += 360;
		
		real_part_of_complex_from_old_code = sin(bearing_to_target_relative_to_wind*PI / 180)*distance_to_target;
		imaginary_part_of_complex_from_old_code = cos(bearing_to_target_relative_to_wind*PI / 180)*distance_to_target;

		
		if (global.debug_handler.path_finding_debug) {
			Serial.println("");
			Serial.print("distance_to_target: ");
			Serial.print(distance_to_target);
			Serial.println("");
			Serial.print("bearing_to_target: ");
			Serial.print(bearing_to_target);
			Serial.println("");
			Serial.print("bearing_to_target_relative_to_wind: ");
			Serial.print(bearing_to_target_relative_to_wind);
			Serial.println("");
			Serial.print("real_part_of_complex_from_old_code: ");
			Serial.print(real_part_of_complex_from_old_code);
			Serial.println("");
			Serial.print("imaginary_part_of_complex_from_old_code: ");
			Serial.print(imaginary_part_of_complex_from_old_code);
			Serial.println("");
		}

	}

	void determine_path_bearing(void){

		//determine global.path_bearing,
		switch (state)
		{
		case close_hauled_wind_from_left:
			global.desired_heading = int(global.global_wind_bearing + TACKING_ZONE)% 360;

			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("close_hauled_wind_from_left");
			}
			break;
		case close_hauled_wind_from_right:
			global.desired_heading = int(global.global_wind_bearing - TACKING_ZONE) % 360;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("close_hauled_wind_from_right");
			}
			//do stuff
			break;

			/*	tacking_going_from_wind_from_left_to_right,
	tacking_going_from_wind_from_right_to_left,
	*/
		case tacking_going_from_wind_from_left_to_right: 
			//h�ns
			global.desired_heading = frozen_tacking_direction;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("tacking_going_from_wind_from_left_to_right");
			}
			//do stuff
			break;
		case tacking_going_from_wind_from_right_to_left:
			//h�ns
			global.desired_heading = frozen_tacking_direction;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("tacking_going_from_wind_from_right_to_left");
			}
			//do stuff
			break;
		case generel_direction_wind_from_right:
			global.desired_heading = bearing_to_target;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("generel_direction_wind_from_right");
			}
			//do stuff
			break;
		case generel_direction_wind_from_left:
			global.desired_heading = bearing_to_target;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("generel_direction_wind_from_left");
			}
			//do stuff
			break;
		case down_wind_wind_from_right:
			global.desired_heading = int(global.global_wind_bearing - DOWN_WIND_ZONE) % 360;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("down_wind_wind_from_right");
			}
			break;

		case down_wind_wind_from_left:
			global.desired_heading = int(global.global_wind_bearing + DOWN_WIND_ZONE) % 360;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("down_wind_wind_from_left");
			}
			break;
		case jibe_going_from_wind_from_left_to_right:
			global.desired_heading = int(global.bearing_container.compass_bearing - TACKING_TURNING_VALUE) % 360;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("jibe_going_from_wind_from_left_to_right");
			}
			break;
		case jibe_going_from_wind_from_right_to_left:
			global.desired_heading = int(global.bearing_container.compass_bearing + TACKING_TURNING_VALUE) % 360;
			if (global.debug_handler.path_finding_debug){
				Serial.print("state is:");
				Serial.println("jibe_going_from_wind_from_right_to_left");
			}
			break;
		default:
			Serial.println("state logic failed, should not be here");
			break;
		}

		if (global.desired_heading > 360) global.desired_heading -= 360;
		if (global.desired_heading < 0) global.desired_heading += 360;

	}

	void next_state_logic(void){
		/*
		close_hauled_wind_from_left, 
		close_hauled_wind_from_right,
		generel_direction_wind_from_right,
		generel_direction_wind_from_left,
		jibe_going_from_wind_from_left_to_right,
		jibe_going_from_wind_from_right_to_left,
		down_wind_wind_from_right,
		down_wind_wind_from_left,
		*/
		//then next state logic
		next_state = state; //default is staying in the current state
		switch (state)
		{
		case close_hauled_wind_from_left:
			if (bearing_to_target_relative_to_wind > TACKING_ZONE) {
				next_state = generel_direction_wind_from_left;
			}
			else if (bearing_to_target_relative_to_wind < (-TACKING_ZONE)){
				next_state = generel_direction_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind < (-TACKING_ZONE_WIDE_ANGLE) || bearing_to_target_relative_to_wind < -TACKING_ZONE_NARROW_ANGLE && real_part_of_complex_from_old_code > TACKING_ZONE_DISTANCE){
				next_state = tacking_going_from_wind_from_left_to_right;
				has_boat_been_at_tacking_target = false;
				frozen_tacking_direction = global.global_wind_bearing - TACKING_ZONE;
				if (global.global_wind_bearing < 0) global.global_wind_bearing += 360;
			}

			break;

		case close_hauled_wind_from_right:
			if (bearing_to_target_relative_to_wind < -TACKING_ZONE) {
				next_state = generel_direction_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind > TACKING_ZONE){
				next_state = generel_direction_wind_from_left;
			}
			/*
			#define TACKING_ZONE_DISTANCE 20
#define TACKING_ZONE_WIDE_ANGLE TACKING_ZONE / 2
#define TACKING_ZONE_NARROW_ANGLE 5
			*/
			else if (bearing_to_target_relative_to_wind > (TACKING_ZONE_WIDE_ANGLE) || bearing_to_target_relative_to_wind > TACKING_ZONE_NARROW_ANGLE && real_part_of_complex_from_old_code < -TACKING_ZONE_DISTANCE){
				next_state = tacking_going_from_wind_from_right_to_left;
				frozen_tacking_direction = global.global_wind_bearing + TACKING_ZONE;
				has_boat_been_at_tacking_target = false;
				if (global.global_wind_bearing > 360) global.global_wind_bearing -= 360;
			}

			break;


			/*	tacking_going_from_wind_from_left_to_right,
			tacking_going_from_wind_from_right_to_left,
			*/
		case tacking_going_from_wind_from_left_to_right :
			if (angle_between_two_angles(global.bearing_container.compass_bearing, frozen_tacking_direction) < TOLERANCE_FOR_COMPLETED_TACKING && has_boat_been_at_tacking_target == false) {
				has_boat_been_at_tacking_target = true;
				time_stamp_for_tacking = millis();
			}
			if ((millis() - time_stamp_for_tacking) > TIMEOUT_FOR_COMPLETED_TACKING && has_boat_been_at_tacking_target == true) {
				next_state = close_hauled_wind_from_right;
			}
			if (bearing_to_target_relative_to_wind < (-TACKING_ZONE-5)) {
				next_state = generel_direction_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind > (TACKING_ZONE+5)){
				next_state = generel_direction_wind_from_left;
			}

			break;
			/*#define TOLERANCE_FOR_COMPLETED_TACKING 15
#define TIMEOUT_FOR_COMPLETED_TACKING 5000*/
		case tacking_going_from_wind_from_right_to_left :
			if (angle_between_two_angles(global.bearing_container.compass_bearing, frozen_tacking_direction) < TOLERANCE_FOR_COMPLETED_TACKING && has_boat_been_at_tacking_target == false) {
				has_boat_been_at_tacking_target = true;
				time_stamp_for_tacking = millis();
			}
			if ((millis() - time_stamp_for_tacking) > 5000 && has_boat_been_at_tacking_target == true) {
				next_state = close_hauled_wind_from_left;
			}
			if (bearing_to_target_relative_to_wind < (-TACKING_ZONE - 5)) {
				next_state = generel_direction_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind >(TACKING_ZONE + 5)){
				next_state = generel_direction_wind_from_left;
			}
			break;

			break;
		case generel_direction_wind_from_right:
			if (bearing_to_target_relative_to_wind > -TACKING_ZONE) {
				next_state = close_hauled_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind < -DOWN_WIND_ZONE){
				next_state = down_wind_wind_from_right;
			}
			break;

		case generel_direction_wind_from_left:
			if (bearing_to_target_relative_to_wind < TACKING_ZONE) {
				next_state = close_hauled_wind_from_left;
			}
			else if (bearing_to_target_relative_to_wind > DOWN_WIND_ZONE){
				next_state = down_wind_wind_from_left;
			}
			break;

		case jibe_going_from_wind_from_left_to_right:
			if (angle_between_two_angles(global.bearing_container.compass_bearing, int(global.global_wind_bearing - DOWN_WIND_ZONE) % 360) < 20) {
				//we are there
				next_state = down_wind_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind < DOWN_WIND_ZONE && bearing_to_target_relative_to_wind > 0){
				next_state = generel_direction_wind_from_left;
			}
			else if (bearing_to_target_relative_to_wind > -DOWN_WIND_ZONE && bearing_to_target_relative_to_wind < 0){
				next_state = generel_direction_wind_from_right;
			}
			break;

		case	jibe_going_from_wind_from_right_to_left:
			if (angle_between_two_angles(global.bearing_container.compass_bearing, int(global.global_wind_bearing + DOWN_WIND_ZONE) % 360) < 20) {
				//we are there
				next_state = down_wind_wind_from_left;
			}
			else if (bearing_to_target_relative_to_wind < DOWN_WIND_ZONE && bearing_to_target_relative_to_wind > 0){
				next_state = generel_direction_wind_from_left;
			}
			else if (bearing_to_target_relative_to_wind > -DOWN_WIND_ZONE && bearing_to_target_relative_to_wind < 0){
				next_state = generel_direction_wind_from_right;
			}
			break;

		case	down_wind_wind_from_right:
			if (bearing_to_target_relative_to_wind > -DOWN_WIND_ZONE && bearing_to_target_relative_to_wind < 0){
				next_state = generel_direction_wind_from_right;
			}
			else if (bearing_to_target_relative_to_wind < 155 && bearing_to_target_relative_to_wind > DOWN_WIND_ZONE){
				next_state = jibe_going_from_wind_from_right_to_left;
			}
			else if (bearing_to_target_relative_to_wind < DOWN_WIND_ZONE && bearing_to_target_relative_to_wind > 0){
				next_state = generel_direction_wind_from_left;
			}
			break;

		case	down_wind_wind_from_left:
			if (bearing_to_target_relative_to_wind < DOWN_WIND_ZONE && bearing_to_target_relative_to_wind > 0){
				next_state = generel_direction_wind_from_left;
			}
			else if (bearing_to_target_relative_to_wind > -155 && bearing_to_target_relative_to_wind < -DOWN_WIND_ZONE){
				next_state = jibe_going_from_wind_from_left_to_right;
			}
			else if (bearing_to_target_relative_to_wind > -DOWN_WIND_ZONE && bearing_to_target_relative_to_wind < 0){
				next_state = generel_direction_wind_from_right;
			}

			break;

		default:
			Serial.println("error in next boat state logic");
			break;
		}
		//if we want to do special things when changing state, it can be done here.
		

	}

	float angle_between_two_angles(float angle_1, float angle_2){
		float result = angle_1 - angle_2;
		if (result > 180) result -= 360; //if this is true, we went the wrong way around the unit circle
		if (result < -180) result += 360;//if this is true, we went the wrong way around the unit circle
		if (result < 0) result *= -1; //we want the absolute angle in bewteen, we dont care about the sign, so i make sure its always positive.
		return result;
	}


	void rudder_pid_controller()
	{

		float  pValue, integralValue, temp_ang;
		int dHeading;
		dHeading = global.desired_heading - global.bearing_container.compass_bearing; // in degrees
		if (dHeading > 180){
			dHeading -= 360;
		}
		if (dHeading < -180){
			dHeading += 360;
		}
		// Singularity translation
	

		// Limit control statement
		//{
		// P controller
		pValue = GAIN_P * dHeading;

		// Integration part
		// The following checks, will keep integratorSum within -0.2 and 0.2
		/*
		if (integratorSum < -INTEGRATOR_MAX && dHeading > 0) {
			integratorSum = dHeading + integratorSum;
		}
		else if (integratorSum > INTEGRATOR_MAX && dHeading < 0) {
			integratorSum = dHeading + integratorSum;
		}
		else {
			integratorSum = integratorSum;
		}
		integralValue = GAIN_I * integratorSum;
		// result
		*/
		temp_ang = pValue;// +integralValue; // Angle in radians

	
		global.Rudder_Desired_Angle = round(temp_ang);

		if (global.Rudder_Desired_Angle > RUDDER_LIMIT) global.Rudder_Desired_Angle = RUDDER_LIMIT;
		if (global.Rudder_Desired_Angle < -RUDDER_LIMIT) global.Rudder_Desired_Angle = -RUDDER_LIMIT;

		//Rudder Calibration can be done here
		Rudder_Servo.write(90 - global.Rudder_Desired_Angle);

		if (global.debug_handler.rudder_and_sail_control_debug) {
			Serial.println("");
			Serial.print("d_heading on rudder: ");
			Serial.print(dHeading);
			Serial.println("");
			Serial.print("Rudder_Desired_Angle: ");
			Serial.print(global.Rudder_Desired_Angle);
			Serial.println("");
			Serial.print("global.desired_heading: ");
			Serial.print(global.desired_heading);
			Serial.println("");
			Serial.print("global.bearing_container.compass_bearing: ");
			Serial.print(global.bearing_container.compass_bearing);
			Serial.println("");
			Serial.print("global.global_wind_bearing: ");
			Serial.print(global.global_wind_bearing);
			Serial.println("");

		}

	}

};



#endif
