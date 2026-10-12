-- Observe every completed rival camera callback through all three route sections.
-- Only mission selection and the player's approach positions are supplied.
-- Original controller/player/camera/collision callbacks remain in charge.
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
local stage=tonumber(os.getenv('JGR_REFERENCE_STAGE'))
local mask=stage==3 and os.getenv('JGR_REFERENCE_CHAPTER')=='4'
assert((stage==1 or stage==2 or stage==3) and (os.getenv('JGR_REFERENCE_CHAPTER')=='2' or mask),'unsupported gang chase')
JGR_CHALLENGE_REFERENCE=true
local approaches=mask and {{1340,0.6806788444519043,-720},{970,-70.1910629272461,-950},{1340,-70.19103240966797,-1050}} or
 stage==1 and {{-400,493,-1380},{-174,207,-515},{-195,100,-130}} or
 stage==3 and {{-80,94.5,-699},{-528,104.5,-1072},{618,74.5,-1071}} or
 {{226.43286,-415,619.736},{-39,-325,920},{-20,-320,1540}}
dofile(root..'tools/flycast_stage_reference.lua');local boot=flycast_callbacks.vblank
local m=flycast.memory;local out=assert(os.getenv('JGR_REFERENCE_OUT'))
local poses=os.getenv('JGR_POISON_POSES')=='1'
local stride=poses and 0x1060 or 0xc60
local age,phase,actors=0,-1,{}
local log=assert(io.open(out..'routes.csv','w'))
log:write('age,slot,sample,segment,started,record,repeat,tick,x,y,z,state,motion,frame,yaw,speed,camera_yaw\n')
local function valid(p)return p>=0x8c000000 and p<0x8d000000 end
local function rf(a)return (string.unpack('<f',string.pack('<I4',m.read32(a))))end
local function wf(a,v)m.write32(a,string.unpack('<I4',string.pack('<f',v)))end
local function tasks()
 local a={};local function walk(t)
  while valid(t) and #a<2000 do a[#a+1]=t;walk(m.read32(t+0x1c));t=m.read32(t+0x24)end
 end;walk(m.read32(0x8c2e9c9c));return a
end
local function bytes(p,n)
 local a={};local words=m.readTable32(p,n//4)
 for j=0,n-1,4 do a[#a+1]=string.pack('<I4',words[p+j] or 0)end;return table.concat(a)
end
local function install(index,p,c,controller)
 local code,serial,dest=0x8cfe4000+index*0x200,0x8cfe5000+index*4,0x8cfe8000+index*0x2000
 local words,loads={},{};local function op(v)words[#words+1]=v end
 local function ld(r,v)loads[#loads+1]={#words,r,v};op(0)end
 local function copy(from,to,n)
  ld(1,from);ld(2,to);ld(3,n//4);op(0x6016);op(0x2202);op(0x7204);op(0x4310);op(0x8bfa)
 end
 local function bump()ld(1,serial);op(0x6012);op(0x7001);op(0x2102)end
 op(0x4f22);ld(4,c);ld(0,0x8c01dc40);op(0x400b);op(9)
 bump();copy(p,dest,0x900);copy(c,dest+0x900,0x290);copy(controller,dest+0xb90,0xd0)
 if poses then
  -- +594 owns the original completed 20-node local skeletal pose.
  ld(1,p+0x594);op(0x6112);ld(2,dest+0xc60);ld(3,0x100)
  op(0x6016);op(0x2202);op(0x7204);op(0x4310);op(0x8bfa)
 end
 bump()
 op(0x4f26);op(0xb);op(9);if #words%2~=0 then op(9)end
 local pool=code+#words*2
 for j,l in ipairs(loads)do local pc=code+l[1]*2;local a=pool+(j-1)*4
  words[l[1]+1]=0xd000+l[2]*256+(a-((pc+4)&0xfffffffc))//4;m.write32(a,l[3])end
 for j,w in ipairs(words)do m.write16(code+(j-1)*2,w)end
 m.write32(serial,0);m.write32(c+8,code)
 local file=assert(io.open(out..'rival'..index..'.bin','wb'));file:write(poses and 'PRT2' or 'PRT1',string.pack('<I4',stride))
 actors[index+1]={p=p,c=c,controller=controller,serial=serial,dest=dest,last=0,count=0,file=file}
 print(string.format('POISON_ROUTE installed slot=%d player=%08x camera=%08x controller=%08x',index,p,c,controller))
end
local function relocate(p,x,y,z)
 -- Invoke the original relocation/reset and locomotion entry points. Raw
 -- position writes leave cached floor contacts at the old spawn location.
 local code,record=0x8cfe7000,0x8cfe7800
 local words,loads={},{};local function op(v)words[#words+1]=v end
 local function ld(r,v)loads[#loads+1]={#words,r,v};op(0)end
 op(0x4f22);ld(4,p);ld(0,0x8c0198f8);op(0x400b);op(9)
 ld(4,p);ld(5,record);ld(6,1);ld(0,0x8c01aeb0);op(0x400b);op(9)
 ld(4,p);ld(5,1);ld(0,0x8c06bd04);op(0x400b);op(9)
 ld(1,p+8);ld(0,0x8c0198f8);op(0x2102);op(0x4f26);op(0xb);op(9)
 if #words%2~=0 then op(9)end;local pool=code+#words*2
 for j,l in ipairs(loads)do local pc=code+l[1]*2;local a=pool+(j-1)*4
  words[l[1]+1]=0xd000+l[2]*256+(a-((pc+4)&0xfffffffc))//4;m.write32(a,l[3])end
 for j,w in ipairs(words)do m.write16(code+(j-1)*2,w)end
 for a=0,0x30,4 do m.write32(record+a,0)end
 wf(record+8,x);wf(record+12,y);wf(record+16,z);m.write32(record+24,tonumber(os.getenv('JGR_SEWER_YAW') or '0'))
 m.write32(p+8,code)
end

flycast_callbacks.vblank=function()
 boot();if not JGR_REFERENCE_READY then return end;age=age+1
 if age>=200 and age<2100 and age%120<5 then flycast.input.pressButtons(1,4)end
 local ts=tasks();local player=0
 for _,t in ipairs(ts)do if (m.read32(t+4)&65535)==0x6a then player=t end end
 if age>=2100 and #actors<3 then
  for _,t in ipairs(ts)do if m.read32(t+8)==0x8c0278c0 and m.read32(t+0x84)==3 then
   local id=m.read32(t+0x90);local index=id-107
   if index>=0 and index<3 and not actors[index+1]then
    for _,p in ipairs(ts)do if (m.read32(p+4)&65535)==id then
     local camera=m.read32(p+0x850);if valid(camera) and m.read32(camera+8)==0x8c01dc40 then
      if mask then
       assert(m.read32(p+0x7c)==15,'Behind the Mask character mismatch')
       local table=m.read32(t+0xbc)
       assert(table==({0x8cde7e7c,0x8cde93a8,0x8cdea40c})[index+1],'Behind the Mask pursuit subset mismatch')
      end
      install(index,p,camera,t)
     end
    end end
   end
  end end
 end
 if age==2200 then assert(#actors==3);relocate(player,table.unpack(approaches[1]));phase=0;print('POISON_ROUTE approach segment 0')end
 for index,a in ipairs(actors)do
  local serial=m.read32(a.serial)
  if serial%2==0 and serial~=a.last then
   assert(serial==a.last+2,'missed completed callback');a.last=serial;a.count=a.count+1
   a.file:write(bytes(a.dest,stride));a.file:flush()
   local p,c,t=a.dest,a.dest+0x900,a.dest+0xb90
   log:write(string.format('%d,%d,%d,%d,%d,%08x,%d,%d,%.9g,%.9g,%.9g,%d,%d,%.9g,%d,%.9g,%d\n',age,index-1,a.count,m.read32(t+0xc0),m.read32(t+0xb8),m.read32(t+0x70),m.read32(t+0x78),m.read32(p+0x98),rf(p+0xb8),rf(p+0xbc),rf(p+0xc0),m.read32(p+0x70),m.read32(p+0x78),rf(p+0x5ac),m.read32(p+0x168),rf(p+0xdc),m.read32(c+0xc4)));log:flush()
  end
 end
 if phase>=0 then
  -- Some Love Attack / Behind the Mask groups span two activation radii.
  -- Approach any remaining member through the player's normal relocation;
  -- never move a rival or change its original controller state.
  if (stage==1 or mask) and age%240==0 then
   for _,a in ipairs(actors)do
    if m.read32(a.controller+0xc0)==phase and m.read32(a.controller+0xb8)==0 then
     relocate(player,rf(a.p+0xb8),rf(a.p+0xbc),rf(a.p+0xc0)+40);break
    end
   end
  end
  local next=(phase+1)%3;local ready=true
  for _,a in ipairs(actors)do if m.read32(a.controller+0xc0)~=next or m.read32(a.controller+0xb8)~=0 then ready=false end end
  if ready then
   if phase==2 then
    for _,a in ipairs(actors)do a.file:close()end;log:close();print('POISON_ROUTES complete all nine segments and return-to-start');print('GANG_ROUTES complete stage='..stage);io.stdout:flush();os.exit(0)
   end
   phase=phase+1
   relocate(player,table.unpack(approaches[phase+1]));print('POISON_ROUTE approach segment '..phase);io.stdout:flush()
  end
 end
 if age%1200==0 then print('POISON_ROUTE age='..age..' phase='..phase);io.stdout:flush()end
end
