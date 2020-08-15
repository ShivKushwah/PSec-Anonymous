secure_machine ClientEnclave {
    var masterSecret: secure_StringType;
    var clientUSM : machine_handle;
    
    start state Initial {
        defer GenerateOTPCodeEvent;
        entry {
            print "MEASURE TRUSTED CREATE END:";
            MeasureTime();
        }
        on TRUSTEDMeasureEvent1 do (payload: (fst:secure_int, snd:secure_StringType)) {
            print "MEASURE TRUSTED SEND END:";
            MeasureTime();
        }
        on TRUSTEDMeasureEvent2 do (payload: (fst:secure_int, snd:secure_StringType)) {
            print "MEASURE TRUSTED SEND 2 END:";
            MeasureTime();
        }
        on TRUSTEDProvisionClientSSM do (payload : secure_machine_handle) {
            clientUSM = Declassify(payload) as machine_handle;
            goto ReceiveMasterSecretEvent;
        }
    }

    state ReceiveMasterSecretEvent {
        defer GenerateOTPCodeEvent;
        on MasterSecretEvent goto ProvisionEnclaveWithSecret;
    }

    state ProvisionEnclaveWithSecret {
        entry (payload : secure_StringType){
            masterSecret = payload;
            goto WaitForGenerateOTP;
        }
    }

    state WaitForGenerateOTP {
        on GenerateOTPCodeEvent do (usernamePassword: StringType) {
            var hashedString : StringType;          
            hashedString = Hash(Declassify(masterSecret) as StringType, usernamePassword);
            send clientUSM, OTPCodeEvent, hashedString; //untrusted_send
        }
    }

}

machine ClientWebBrowser {
    var clientSSM: machine_handle;
    var bankSSM: machine_handle;
    var usernamePassword: StringType;
    var OTPCode: StringType;
    start state Initial {
        defer PublicIDEvent;
        on BankPublicIDEvent goto SaveBankSSM;
    }

    state SaveBankSSM {
        entry (payload: machine_handle) {
            bankSSM = payload;
            goto RegisterAccountInBank;
        }
    }

    state RegisterAccountInBank {
        entry {
            var credentials : StringType;
            credentials = GetUserInput();
            print "MEASURE UNTRUSTED SEND START:";
            MeasureTime();
            send bankSSM, MeasureEvent1, (fst = 1, snd = GetHelloWorld());
            print "MEASURE UNTRUSTED SEND 2 START:";
            MeasureTime();
            send bankSSM, MeasureEvent2, (fst = 1, snd = GetHelloWorld());

            send bankSSM, UNTRUSTEDReceiveRegistrationCredentials, credentials;
        }
        on PublicIDEvent goto Authenticate;
    }
    
    state Authenticate {
        entry (payload: machine_handle) {
            clientSSM = payload;
            print "Client Web Browser: Enter Credentials to login to bank!\n";
            usernamePassword = GetUserInput();
            goto RequestOTPCodeGeneration;
        }
    }

    state RequestOTPCodeGeneration {
        entry {
            send clientSSM, GenerateOTPCodeEvent, usernamePassword; //untrusted_send
            receive {
                case OTPCodeEvent : (payload : StringType) {
                    goto SaveOTPCode, payload;
                }
            }
        }
    }

    state SaveOTPCode {
        entry (payload : StringType) {
            print "OTP Code Received:\n";
            PrintString(payload); 
            OTPCode = payload;
            goto ValidateOTPCode;
        }

    }

    state ValidateOTPCode {
        entry {
            send bankSSM, AuthenticateRequest, (usernamePW = usernamePassword, OTPCode = OTPCode); //untrusted_send
            receive {
                case AuthSuccess : {
                    goto Done;
                }
                case AuthFailure : {
                    print "Authentication Failed!";
                    print "Client Web Browser: Reenter Credentials to login!";
                    usernamePassword = GetUserInput();
                    goto RequestOTPCodeGeneration;
                }
            }
        }
        
    }

    state Done {
        entry {
            var test1: int;
            var test2: map[int, int];
            var test3: (int, int);

            var sealedDataDump : (sealed_data_key, sealed_data);

            test1 = 7;
            sealedDataDump = seal(test1);
            if (unseal(sealedDataDump.0, sealedDataDump.1) as int == 7) {
                print "First Seal Test Success!";
            }
            
            test2[3] = 8;
            sealedDataDump = seal(test2);
            if ((unseal(sealedDataDump.0, sealedDataDump.1) as map[int, int])[3] == 8) {
                print "Second Seal Test Success!";
            }

            test3 = (9, 9);
            sealedDataDump = seal(test3);
            if ((unseal(sealedDataDump.0, sealedDataDump.1) as (int, int)).1 == 9) {
                print "Third Seal Test Success!";
            }

            print "Client Web Browser Authenticated Successfully!";
        }
     }

}