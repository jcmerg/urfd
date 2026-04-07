<?php

class Node {

   private $Callsign;
   private $IP;
   private $LinkedModule;
   private $LinkedModuleTS1;
   private $LinkedModuleTS2;
   private $Protocol;
   private $ConnectTime;
   private $LastHeardTime;
   private $Suffix;
   private $Prefix;
   private $RandomID;

   public function __construct($Callsign, $IP, $LinkedModule, $Protocol, $ConnectTime, $LastHeardTime, $RandomID) {

      $this->IP            = $IP;

      $this->Protocol      = $Protocol;
      $this->ConnectTime   = ParseTime($ConnectTime);
      $this->LastHeardTime = ParseTime($LastHeardTime);
   
      $this->FullCallsign  = trim(str_replace("   ", "-", $Callsign));
      $this->FullCallsign  = str_replace("  ", "-", $this->FullCallsign);
      $this->FullCallsign  = str_replace(" ", "-", $this->FullCallsign); 
      
      if (strpos($Callsign, " ") !== false) {
         $this->Callsign      = trim(substr($Callsign, 0, strpos($Callsign, " ")));
         $this->Suffix        = trim(substr($Callsign, strpos($Callsign, " "), strlen($Callsign)));
         $this->Prefix        = strtoupper(trim(substr($Callsign, 0, 3)));
      }
      else {
         $this->Callsign      = trim($Callsign);
         $this->Suffix        = "";
         $this->Prefix        = "";
      }

      $this->LinkedModule     = trim($LinkedModule);
      $this->LinkedModuleTS1  = '';
      $this->LinkedModuleTS2  = '';
      $this->RandomID         = $RandomID;
   }

   public function SetLinkedModuleTS1($m)    { $this->LinkedModuleTS1 = trim($m); }
   public function SetLinkedModuleTS2($m)    { $this->LinkedModuleTS2 = trim($m); }

   public function GetFullCallsign()         { return $this->FullCallsign;   }
   public function GetCallsign()             { return $this->Callsign;       }
   public function GetIP()                   { return $this->IP;             }
   public function GetLinkedModule()         { return $this->LinkedModule;   }
   public function GetLinkedModuleTS1()      { return $this->LinkedModuleTS1; }
   public function GetLinkedModuleTS2()      { return $this->LinkedModuleTS2; }
   public function GetProtocol()             { return $this->Protocol;       }
   public function GetConnectTime()          { return $this->ConnectTime;    }
   public function GetLastHeardTime()        { return $this->LastHeardTime;  }
   public function GetSuffix()               { return $this->Suffix;         }
   public function GetPrefix()               { return $this->Prefix;         }
   public function GetRandomID()             { return $this->RandomID;       }

   // returns array of all linked modules (for dual-slot MMDVM)
   public function GetAllLinkedModules() {
      $mods = array();
      if ($this->LinkedModuleTS1 != '') $mods[] = $this->LinkedModuleTS1;
      if ($this->LinkedModuleTS2 != '') $mods[] = $this->LinkedModuleTS2;
      if (empty($mods) && $this->LinkedModule != '') $mods[] = $this->LinkedModule;
      return array_unique($mods);
   }

   public function IsLinkedTo($mod) {
      $all = $this->GetAllLinkedModules();
      return in_array($mod, $all);
   }
   
}

?>
