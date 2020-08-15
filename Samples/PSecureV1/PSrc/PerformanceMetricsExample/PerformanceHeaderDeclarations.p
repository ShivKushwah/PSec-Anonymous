//Events
event MeasureEvent1 : (fst: int, snd: StringType);
event MeasureEvent2 : (fst: int, snd: StringType);
event BankPublicIDEvent : machine_handle;
event PublicIDEvent : machine_handle;
event UNTRUSTEDReceiveRegistrationCredentials : StringType;
event GenerateOTPCodeEvent : StringType;
event OTPCodeEvent : StringType;
event AuthenticateRequest : (usernamePW : StringType, OTPCode : StringType);
event AuthSuccess;
event AuthFailure;

trusted event TRUSTEDProvisionBankSSM : secure_machine_handle;
trusted event TRUSTEDProvisionClientSSM : secure_machine_handle;
trusted event TRUSTEDMeasureEvent1 : (fst: secure_int, snd: secure_StringType);
trusted event TRUSTEDMeasureEvent2 : (fst: secure_int, snd: secure_StringType);
trusted event MasterSecretEvent: secure_StringType;
trusted event MapEvent: map[secure_int, secure_int];

//Functions
fun PrintString(inputString : StringType);
fun PrintRawStringType(inputString : StringType);
fun PrintRawSecureStringType(inputString : secure_StringType);
fun Concat(input1:StringType, input2:StringType) : StringType;
fun Hash(input1:StringType, input2:StringType) : StringType;
fun PrintKey(input : machine_handle);
fun GenerateRandomMasterSecret() : secure_StringType;
fun GetUserInput() : StringType;
fun GetHelloWorld() : StringType;
fun MeasureTime();
fun EXIT();