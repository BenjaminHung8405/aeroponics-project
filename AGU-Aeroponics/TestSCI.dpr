program TestSCI;

{$APPTYPE CONSOLE}

uses
  SysUtils,
  SCIW32,
  console,
  MyString;

var sci:TSCI;
    v:byte;


Function ReadFrame:byte;
var v:byte;
begin
  ReadFrame:=0;

  sci.Rec(v);
  if v=$ff then begin
     sci.rec(v);
     if v=$5a then begin
        if sci.Rec(v) then ReadFrame:=v;
     end;
  end;
  sci.rec(v);
end;

Procedure WriteEEP(a:word;v:byte);   // write EEP
var k,l:word; ok:boolean;
    t:byte;
begin
  k:=5;
  ok:=false;
  repeat
     sci.SendCom(#$09+chr(hi(a))+chr(lo(a))+chr(v));
     sleep(1);

     for l:=1 to 2 do
       if sci.rec(t) then begin
          ok:=t=$5a;    // read ACK
          break;
       end;

     if not ok then begin
        write(chr(v));
        sleep(20);
     end;

     dec(k);
  until ok or (k=0) or keypressed;
end;

Function ReadEEP(a:word):byte;
var v,k:byte;
    ok:boolean;
begin
  ok:=false;
  result:=0;
  k:=5;
  repeat
     sci.SendCom(#$08+chr(hi(a))+chr(lo(a)));
     if sci.rec(v) then begin
        result:=v;
        ok:=true;
        break;
     end else sleep(10);
     dec(k);
  until ok or (k=0);
end;

Procedure WrStrEEP(a:word;s:string);
var k:word; v:char;
begin
  for k:=a to a+length(s)-1 do begin
     v:=s[k-a+1];
     write(v);
     WriteEEP(k,byte(v));
//     sleep(100);
  end;
end;

Function ReadMemw(a:word;var w:word):boolean;
var v1,v2:byte;  k:byte;
    s:string;
begin
  result:=false;
  for k:=1 to 6 do begin
      s:=#$01+chr(lo(a))+chr(hi(a))+#$02;
//      writeln;
//      writeln('Sendcom: ',itoh(byte(s[1]),2),itoh(byte(s[2]),2),itoh(byte(s[3]),2),itoh(byte(s[4]),2));

      sci.SendCom(s);
      if sci.rec(v1) then if sci.rec(v2) then begin
         w:=v1 or (v2 shl 8);
         result:=true;
         break;
      end;
      sleep(10);
  end;
end;

Function FReadB:byte;  // frame read byte
var v:byte;
begin
  result:=0;
  v:=0;
//  repeat  if not sci.rec(v) then exit;  until v=$ff;
//  v:=0;
 // repeat  if not sci.Rec(v) then exit;  until v=$5a;

  if not sci.rec(v) then exit;
  result:=v;

//  sci.Rec(v);   // read end frame byte
end;

Function FReadW:word;   // frame read word
var v:byte;
begin
  result:=0;
//  repeat  if not sci.rec(v) then exit;  until v=$ff;
//  repeat  if not sci.Rec(v) then exit;  until v=$5a;

  if not sci.rec(v) then exit;
  result:=v;   // lo byte
  if not sci.Rec(v) then exit;
  result:=result or (v shl 8);  // add hi byte

  sci.Rec(v);   // read end frame byte
end;

Function FReadBC(var v:byte):boolean;  // frame read byte with checksum
var c:byte;
begin
  result:=false;
//  repeat  if not sci.rec(v) then exit;  until v=$ff;
//  repeat  if not sci.Rec(v) then exit;  until v=$5a;

  if not sci.rec(v) then exit;
  if not sci.rec(c) then exit;

//  v:=c;
//  result:=true;

  if ((c+v)and $ff)=0 then result:=true;
end;

Function FReadWC:word;   // frame read word with checksum
var v,v1,c:byte;
begin
  result:=0;
//  repeat  if not sci.rec(v) then exit;  until v=$ff;
//  repeat  if not sci.Rec(v) then exit;  until v=$5a;

  if not sci.rec(v) then exit;  // lo byte
  if not sci.Rec(v1) then exit; // hi byte

  if not sci.rec(c) then exit;  // chks byte

  if ((v+v1+c)and $ff)=0 then result:=v or (v1 shl 8);  // add hi byte

//  sci.Rec(v);   // read end frame byte
end;




Procedure Test1;
var v,b:byte;
    ok:boolean;

    Procedure testid(v,id:byte);
    begin
       write('Send value to #',itoh(id,2),': '+itoh(v,2));
       sci.SendCom(#$05+chr(v)+chr(id));
       write('   receive from #',itoh(id,2),'...');
       ok:=false;
       b:=FReadB;
       if b=v then ok:=true;

       if ok then writeln(itoh(v,2))
       else writeln('fail!     #',itoh(id,2),'   (recvalue: ',itoh(b,2),')');
       sleep(5);
    end;


begin
  v:=0;
  randomize;
  repeat
     v:=random(256);
     testid(random(256),3);
 //   testid(random(256),$f0);

     //inc(v);

     sleep(200);
  until keypressed;
end;

Procedure Test2;  // read RAM
var v1,v2,b,ch,k,l:byte;
    s:string;
    ok:boolean;

    addr:word;

    buf:array [0..31] of byte;

    Function Read8BC(var buf):boolean;  // doc 8 byte co1 checksum
    var a:array[0..7] of byte absolute buf;
        chks,v,k:byte;
    begin
      chks:=0;
      result:=false;
      for k:=0 to 7 do begin
          if sci.rec(v) then a[k]:=v
          else exit;
          chks:=chks+v;
      end;

      if sci.Rec(v) then chks:=chks+v else exit;
      if chks=0 then Result:=true;
    end;

begin
  v:=0;
  ch:=0;

  repeat
     ok:=true;

     addr:=8;

     sci.SendCom(#$0e+chr(lo(addr))+chr(hi(addr))+#$08+#$01);    // doc 8 byte tu 0000
     if Read8BC(buf[0]) then begin
        addr:=addr+8;
        sci.SendCom(#$0e+chr(lo(addr))+chr(hi(addr))+#$08+#$01);    // doc 8 byte tu 0008
        if Read8BC(buf[8]) then begin

        end else ok:=false;
     end else ok:=false;

     if ok then begin
        for k:=0 to 15 do write(itoh(buf[k],2):3);
     end else write('Read failed!');

     writeln;

     sleep(15);
  until keypressed;
end;


Procedure Test3;      // write RAM
var v,b,k:byte;
    ok:boolean;
begin
  v:=0;
  write('Write to RAM: ');
  for b:=0 to 31 do begin
     v:=b+64;
     write(itoh(v,2):3);
     for k:=1 to 4 do begin
       sci.SendCom(#$04+chr(b)+#$00+chr(v)+#$01);
       if sci.Rec(v) then if v=$5a then break;
     end;

    // sleep(1);
  end;
  writeln;
  readln;
end;


Procedure Test4;
var v,b:byte;
    ok:boolean;
begin
  v:=$A5;
  repeat
     ok:=false;
     writeln('Send... ',itoh(v,2));
     if sci.Send(v) then ok:=true;

     sleep(500);
  until keypressed;
end;


Procedure Test5;
var v,b:byte;
    ok:boolean;
begin
  v:=$A5;
  repeat
     ok:=false;

     if sci.rec(v) then write(itoh(v,2):4);

 //    sleep(5);
  until keypressed;
end;


Procedure recTO(n:byte;T:int);
var i,l:int;  v:byte;
begin
     if n<1 then exit;
     i:=0;
     l:=0;
     repeat
        if sci.Rec(v) then begin
          inc(l);
          if l=1 then write(' response: ');
          write(itoh(v,2));
          if l>=n then break;
        end else begin write(' timeout'); break end;

//        dec(t);
//        sleep(1);
//        if t=0 then begin write(' timeout'); break; end;
     until false;
     writeln;
end;


Procedure Test6(id:byte); // on/off pump
var v,b:byte;
    t:int;
    ok:boolean;
begin
  v:=$A5;
  repeat
     ok:=false;
     write('On pump...');

//     sci.sendcom(#$06+chr($14));   //recTO(1,300);   gid==14 [4,5,6,7]

     sci.sendcom(#$06+chr(4));
     recTO(1,300);
     sci.sendcom(#$06+chr(5));
     recTO(1,300);
     sci.sendcom(#$06+chr(6));
     recTO(1,300);
     sci.sendcom(#$06+chr(7));
     recTO(1,300);
             //      exit;
     sleep(3000);

//     writeln('Off pump...');
//     sci.sendcom(#$07+chr($14));     //  recTO(1,300);

     sci.sendcom(#$07+chr(4));
     recTO(1,300);
     sci.sendcom(#$07+chr(5));
     recTO(1,300);
     sci.sendcom(#$07+chr(6));
     recTO(1,300);
     sci.sendcom(#$07+chr(7));
     recTO(1,300);

     sleep(10000);

 //    sleep(5);
  until keypressed;
end;


Procedure SetID;
var v,id:byte;  s:string;
begin
  sci.SendCom(#$0a#$00);
  id:=ReadFrame;
  writeln('Current ID=',itoh(id,2));
  write('Set new ID: ');
  readln(s);
  id:=stoi(s);
  write('Set new ID: ',itoh(id,2));
  sci.SendCom(#$0a#$01+chr(id));
  writeln(' ...');
end;

Procedure sendstr(s:string);
var k:byte;
begin
  for k:=1 to length(s) do sci.Send(byte(s[k]));
end;


Function recstr:string;
var v:byte;
begin
  result:='';

  while sci.Rec(v) do begin
      result:=result+chr(v);
  end;
end;


Procedure RFSetup;
begin
  writeln('Press enter to begin setup...');
  readln;

  sci.Init(COM6,9600,0);
  if paramstr(1)='-' then SetID;

  writeln('Send AT');
  sendstr('AT'#13#10);
  writeln('Rec string: ',recstr);

  delay(500);
  writeln('Send AT+B38400');
  sendstr('AT+B38400'#13#10);
  writeln('Rec string: ',recstr);

  delay(500);
  writeln('Send AT+UN2');
  sendstr('AT+UN2'#13#10);
  writeln('Rec string: ',recstr);

  delay(500);
  writeln('Send AT+A123');
  sendstr('AT+A123'#13#10);
  writeln('Rec string: ',recstr);

  delay(500);
  writeln('Send AT+C001');
  sendstr('AT+C001'#13#10);
  writeln('Rec string: ',recstr);

  writeln('Setup done. Press enter.');

  readln;
  sci.cancel;
  delay(1000);

end;


begin
  { TODO -oUser -cConsole Main : Insert code here }

//    RFSetup;

  sci.Init(COM6,38400,0);
  if paramstr(1)='-' then SetID;

//  test5;

// test3;

  test6(1);

  sci.cancel;
end.
