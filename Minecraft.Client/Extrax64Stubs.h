void Discord_SetJoinSecret(const char* hostIp, WORD port);
bool Discord_HasPendingJoin(char* outIP, int* outPort);
void TickDiscord();