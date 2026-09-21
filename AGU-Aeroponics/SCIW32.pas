unit SCIW32;

interface

Uses Windows;

Const HERROR=INVALID_HANDLE_VALUE;
      COM1=0;
      COM2=1;
      COM3=2;
      COM4=3;
      COM5=4;
      COM6=5;
      COM7=6;
      COM8=7;
      COM9=8;

Type  int=smallint;
      lint=longint;

Type TSCI=Object
            hcom:THandle;
            IDPort:int;
            BaudRate:lint;
            DataBits:int;
            Parity:int;
            StopBits:int;
            ReadTimeOut:lint;
            WriteTimeOut:lint;
            ReadBufSz:int;
            WriteBufSz:int;

            Constructor Init(id:int;br:lint;prbit:int);
            Function    Rec(var val:byte):boolean;
            Function    Send(val:byte):boolean;
            Procedure   FreeBuf;
            Function    SendCom(s:shortstring):boolean;

            Function  FReadB:byte;  // frame read byte

            Procedure EchoTest(id:byte;pausetime:longint);  // test echo function 05

            Destructor  cancel;
          end;

implementation

Uses SysUtils, MyString, Console;

Const pName:packed array [0..8,0..4] of char =('COM1'#0,'COM2'#0,'COM3'#0,'COM4'#0,
                                               'COM5'#0,'COM6'#0,'COM7'#0,'COM8'#0,'COM9'#0);
      chgParity:array [0..4] of lint=(NOPARITY,ODDPARITY,EVENPARITY,MARKPARITY,SPACEPARITY);
      chgStopbit:array [0..2] of lint=(ONESTOPBIT,ONE5STOPBITS,TWOSTOPBITS);

Procedure Error(n:int);
var er:longint;
    s:string;
begin
  er:=GetLastError();
  s:=#13'Error code:'+inttostr(er);
  case n of
    1:MessageBox(0,PChar('Cannot access port.'+s),'Error',0);
    2:MessageBox(0,PChar('Cannot init port.'+s),'Error',0);
  end;
  exit;
end;

Constructor TSCI.Init(id:int;br:lint;prbit:int);
Var dcb:TDCB;
    rs:boolean;
    tout:TCOMMTIMEOUTS;
begin
//  if hcom<>HERROR then exit;
  if id>8 then id:=1;   // default is COM2
  hCom := CreateFile(@pName[id],
                     GENERIC_READ + GENERIC_WRITE,
                     0,    // comm devices must be opened w/exclusive-access
                     nil,  // no security attrs
                     OPEN_EXISTING,  // comm devices must use OPEN_EXISTING
                     0,    // not overlapped I/O
                     0     // hTemplate must be NULL for comm devices
                     );
  if (hCom = HERROR) then begin
    Error(1);
    exit;
  end;
  ReadBufSz:=1200;
  WriteBufSz:=1200;
  SetupComm(hcom,ReadbufSz,WriteBufSz);

  rs := GetCommState(hCom,dcb);
  if rs then begin
    dcb.Flags:=$00000001;
    if br<100 then br:=115200 div br;
    dcb.BaudRate := br;
    dcb.ByteSize := 8;
    if prbit>4 then prbit:=0;
    dcb.Parity := chgParity[prbit];
    dcb.StopBits := TWOSTOPBITS;
    if not SetCommState(hCom, dcb) then Error(2);
  end else Error(2);

  tout.ReadIntervalTimeout :=0;
  tout.ReadTotalTimeoutMultiplier:=round(1/br*12*1000);
  tout.ReadTotalTimeoutConstant:=100;
  tout.WriteTotalTimeoutMultiplier:=round(1/br*12*1000);
  tout.WriteTotalTimeoutConstant:=100;
  if not SetCommTimeOuts(hcom,tout) then Error(2);

  PurgeComm(hcom,PURGE_TXABORT+PURGE_RXABORT+PURGE_TXCLEAR+PURGE_RXCLEAR);
end;

Function    TSCI.Rec(var val:byte):boolean;
var rs:DWORD;
begin
  Result:=False;
  if hcom=HERROR then exit;
  Result:=ReadFile(hcom,val,1,rs,nil);
  if Result then Result:=rs=1;
end;

Function    TSCI.Send(val:byte):boolean;
var rs:DWORD;
begin
  Result:=False;
  if hcom=HERROR then exit;
  Result:=WriteFile(hcom,val,1,rs,nil);
  if Result then Result:=rs=1;
end;

Procedure   TSCI.FreeBuf;
Begin
  if hcom=HERROR then exit;
  PurgeComm(hcom,PURGE_TXABORT+PURGE_RXABORT+PURGE_TXCLEAR+PURGE_RXCLEAR);
end;

Function    TSCI.SendCom(s:shortstring):boolean;
Var c,k:byte;
    a:packed array [0..255] of byte absolute s;
begin
  Result:=False;
  if hcom=HERROR then exit;
  if a[0]=0 then exit;
  PurgeComm(hcom,PURGE_TXABORT+PURGE_RXABORT+PURGE_TXCLEAR+PURGE_RXCLEAR);
  inc(a[0]);
  c:=0;
  for k:=0 to a[0]-1 do begin
    if not Send(a[k]) then exit;
    c:=c+a[k];
  end;
  asm not c; inc c end;
  if not Send(c) then exit;
  Result:=True
end;


Function TSCI.FReadB:byte;  // frame read byte
var v:byte;
begin
  result:=0;

{$IFDEF UF}
  repeat  if not rec(v) then exit;  until v=$ff;
  repeat  if not Rec(v) then exit;  until v=$5a;
{$ENDIF}

  if not rec(v) then exit;
  result:=v;
end;

// gui echo test, dung 1 khoang thoi gian pausetime giua cac lan echo

Procedure TSCI.EchoTest(id:byte;pausetime:longint);
var v,b:byte; ok,q:boolean;
begin
  v:=0;
  randomize;
  repeat
     v:=random(256);
     write('Send value to #',itoh(id,2),': '+itoh(v,2));
     SendCom(#$05+chr(v)+chr(id));
     write('   receive from #',itoh(id,2),'...');
     ok:=false;
     b:=FReadB;
     if b=v then ok:=true;

     if ok then writeln(itoh(v,2))
     else writeln('fail!     #',itoh(id,2),'   (recvalue: ',itoh(b,2),')');

     sleep(pausetime);

     q:=keypressed;
  until q;
end;



Destructor  TSCI.cancel;
begin
  if hcom<>HERROR then CloseHandle(hcom);
end;

end.





