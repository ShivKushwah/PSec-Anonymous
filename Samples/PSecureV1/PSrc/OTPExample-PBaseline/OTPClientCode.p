machine ClientEnclave {
    var masterSecret: StringType;
    var clientUSM : machine_handle;
    
    start state Initial {
        defer GenerateOTPCodeEvent;
        on TRUSTEDProvisionClientSSM do (payload : machine_handle) {
            clientUSM = payload;
            goto ReceiveMasterSecretEvent;
        }
    }

    state ReceiveMasterSecretEvent {
        defer GenerateOTPCodeEvent;
        on MasterSecretEvent goto ProvisionEnclaveWithSecret;
    }

    state ProvisionEnclaveWithSecret {
        entry (payload : StringType){
            masterSecret = payload;
            goto WaitForGenerateOTP;
        }
    }

    state WaitForGenerateOTP {
        on GenerateOTPCodeEvent do (password: StringType) {
            var hashedString : StringType;          
            hashedString = Hash(masterSecret, password);
            unencrypted_send clientUSM, OTPCodeEvent, hashedString; //untrusted_unencrypted_send
        }
    }

}

machine ClientWebBrowser {
    var clientSSM: machine_handle;
    var bankSSM: machine_handle;
    var username: StringType;
    var password: StringType;
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
            // print "MEASURE BASELINE START:";
            // MeasureTime();
            
            username = GetUsernameInput();
            password = GetPasswordInput();
            unencrypted_send bankSSM, UNTRUSTEDReceiveRegistrationCredentials, (this, username, password);
        }
        on PublicIDEvent goto Authenticate;
    }
    
    state Authenticate {
        entry (payload: machine_handle) {
            print "MEASURE BASELINE START:";
            MeasureTime();
            
            clientSSM = payload;
            print "Client Web Browser: Enter Credentials to login to bank!\n";
            goto RequestOTPCodeGeneration;
        }
    }

    state RequestOTPCodeGeneration {
        entry {
            var machineTypeToValidate : string;
            machineTypeToValidate = "ClientEnclave";
            // if (localAuthenticate(clientSSM, machineTypeToValidate)) {
            //     print "Authenticated installed enclave!";
            // }
            unencrypted_send clientSSM, GenerateOTPCodeEvent, password; //untrusted_unencrypted_send
            receive {
                case OTPCodeEvent : (payload : StringType) {
                    goto SaveOTPCode, payload;
                }
            }
        }
    }

    state SaveOTPCode {
        entry (payload : StringType) {
            //print "OTP Code Received: {0}\n", payload;
            print "OTP Code Received:\n";
            PrintString(payload); 
            OTPCode = payload;
            goto ValidateOTPCode;
        }

    }

    state ValidateOTPCode {
        entry {
            unencrypted_send bankSSM, UNTRUSTEDAuthenticateRequest, (Username = username, Password = password, OTPCode = OTPCode); //untrusted_unencrypted_send
            receive {
                case AuthSuccess : {
                    goto Done;
                }
                case AuthFailure : {
                    print "Authentication Failed!";
                    print "Client Web Browser: Reenter Credentials to login!";
                    username = GetUsernameInput();
                    password = GetPasswordInput();
                    goto RequestOTPCodeGeneration;
                }
            }
        }
        
    }

    state Done {
        entry {
            print "Client Web Browser Authenticated Successfully!\n";
            print "MEASURE BASELINE END:";
            MeasureTime();
            EXIT();
        }
     }

}