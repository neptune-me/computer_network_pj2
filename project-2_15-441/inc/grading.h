/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the TCP in the Wild course project developed for the
 * Computer Networks course (15-441/641) taught at Carnegie Mellon University.
 *
 * No part of the project may be copied and/or distributed without the express
 * permission of the 15-441/641 course staff.
 *
 *
 * This files defines constants used for grading. Do NOT modify this file as
 * any changes will be overwritten by the autograder.
 */

#ifndef PROJECT_2_15_441_INC_GRADING_H_
#define PROJECT_2_15_441_INC_GRADING_H_

/*
 * DO NOT CHANGE THIS FILE
 * This contains the variables for your TCP implementation
 * and we will replace this file during the autolab testing with new variables.
 */

// packet lengths
#define MAX_LEN 1400

// window variables
#define WINDOW_INITIAL_WINDOW_SIZE (MSS * 16)
#define WINDOW_INITIAL_SSTHRESH (MSS * 64)
#define WINDOW_INITIAL_RTT 3000  // ms
#define WINDOW_INITIAL_ADVERTISED MSS

// Max TCP Buffer
#define MAX_NETWORK_BUFFER 65535  // (2^16 - 1) bytes

#endif  // PROJECT_2_15_441_INC_GRADING_H_
