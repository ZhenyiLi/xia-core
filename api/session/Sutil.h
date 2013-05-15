/*
** Copyright 2011 Carnegie Mellon University
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**    http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
/*!
  @file Sutil.h
  @brief header for internal helper functions
*/  
#ifndef _SUTIL_H
#define _SUTIL_H

#include "session.pb.h"

#ifdef DEBUG
#define LOG(s) fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, s)
#define LOGF(fmt, ...) fprintf(stderr, "%s:%d: " fmt"\n", __FILE__, __LINE__, __VA_ARGS__) 
#else
#define LOG(s)
#define LOGF(fmt, ...)
#endif

int proc_send(int sockfd, session::SessionMsg *sm);
int proc_reply(int sockfd, session::SessionMsg &sm);
//int proc_reply2(int sockfd, xia::XSocketCallType *type);
int bind_to_random_port(int sockfd);

std::vector<std::string> split(const std::string &s, char delim);
std::string trim(const std::string& str);

#endif /* _SUTIL_H */
