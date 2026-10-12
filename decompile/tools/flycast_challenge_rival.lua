-- Observe both original live recruitment rival sections and return-to-start.
-- Mission selection and spectator positions are supplied. Per the review
-- fixture, the player follows each completed rival position; all original
-- mission success/failure rules remain active. No result setter is patched.
-- Original controller/player/camera/collision callbacks remain in charge.
-- JGR_RIVAL_SPECTATOR=stationary supplies only each section's initial
-- approach, then leaves the player still to check for follower interference.
-- Optional end framebuffers show the spectator's view, not a rival camera.
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
JGR_CHALLENGE_REFERENCE=true
local wanted=tonumber(os.getenv('JGR_DEMO_RECORD'));local subset=tonumber(os.getenv('JGR_DEMO_SET'))
local selected=false
dofile(root..'tools/flycast_stage_reference.lua');local boot=flycast_callbacks.vblank
local m=flycast.memory;local out=assert(os.getenv('JGR_REFERENCE_OUT'))
local poses=true
local stride=poses and 0x1060 or 0xc60
local age,phase,actors=0,-1,{}
local followed=-1
local stationary=os.getenv('JGR_RIVAL_SPECTATOR')=='stationary'
local audit_shots={}
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
 local code,serial,dest=0x8cfe4000+index*0x200,0x8cfe5000+index*4,0x8cfe6000+index*0x2000
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
 local code,record=0x8cfdf000,0x8cfdf800
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
local function follow(p,x,y,z,yaw,distance,initial)
 -- Keep the spectator outside body/tag contact range. BAM travel angles
 -- point along (-sin(yaw),-cos(yaw)); add that vector to trail behind.
 local angle=yaw*(2*math.pi/65536)
 distance=distance or 60
 x=x+math.sin(angle)*distance;z=z+math.cos(angle)*distance
 if initial then relocate(p,x,y,z)
 else
  -- Do not repeatedly invoke the spawn/reset guard: the spectator's
  -- normal body, attached tag and collision callbacks must keep updating.
  wf(p+0xb8,x);wf(p+0xbc,y);wf(p+0xc0,z)
  wf(p+0xc4,x);wf(p+0xc8,y);wf(p+0xcc,z)
  m.write32(p+0x168,yaw)
 end
end

flycast_callbacks.vblank=function()
 -- After original stage creation, only this observer is needed. Avoid
 -- running the generic stage screenshot/logger on every rival sample.
 if not JGR_REFERENCE_READY then boot()
 else
  flycast.input.releaseButtons(1,0xffffffff)
  flycast.input.setAxis(1,1,0);flycast.input.setAxis(1,2,0)
 end
 local ts=tasks();local player=0
 for _,t in ipairs(ts)do
  if (m.read32(t+4)&65535)==0x6a then player=t end
  if m.read32(t+8)==0x8c0ca2ac and not selected and m.read32(t+0x6c)==2 then
   m.write32(t+0x90,wanted);m.write32(t+0x94,subset);m.write32(t+0x6c,1);selected=true
   print('CHALLENGE_RIVAL selected record '..wanted..' set '..subset)
  end
 end
 if not JGR_REFERENCE_READY then return end;age=age+1
 if age>=200 and (#actors==0 or actors[1].count<2) and age%120<5 then flycast.input.pressButtons(1,4)end
 if #actors==0 then
  for _,t in ipairs(ts)do if m.read32(t+8)==0x8c0278c0 and m.read32(t+0x84)==3 then
   local id=m.read32(t+0x90)
   assert(id==107,'unexpected rival slot')
   for _,p in ipairs(ts)do if (m.read32(p+4)&65535)==id then
    local camera=m.read32(p+0x850)
    if valid(camera) and m.read32(camera+8)==0x8c01dc40 then
     assert(m.read32(p+0x7c)==tonumber(os.getenv('JGR_DEMO_CHARACTER')),'rival character mismatch')
     install(0,p,camera,t)
    end
   end end
  end end
 end
 for index,a in ipairs(actors)do
  local serial=m.read32(a.serial)
  if serial%2==0 and serial~=a.last then
   assert(serial==a.last+2,'missed completed callback');a.last=serial;a.count=a.count+1
   a.file:write(bytes(a.dest,stride));a.file:flush()
   local p,c,t=a.dest,a.dest+0x900,a.dest+0xb90
   log:write(string.format('%d,%d,%d,%d,%d,%08x,%d,%d,%.9g,%.9g,%.9g,%d,%d,%.9g,%d,%.9g,%d\n',age,index-1,a.count,m.read32(t+0xc0),m.read32(t+0xb8),m.read32(t+0x70),m.read32(t+0x78),m.read32(p+0x98),rf(p+0xb8),rf(p+0xbc),rf(p+0xc0),m.read32(p+0x70),m.read32(p+0x78),rf(p+0x5ac),m.read32(p+0x168),rf(p+0xdc),m.read32(c+0xc4)));log:flush()
   if os.getenv('JGR_REFERENCE_FRAMEBUFFER')=='1' and m.read32(t+0xc0)==1 then
    local tick=m.read32(p+0x98)
    for _,at in ipairs({2400,2497,2624})do
     if tick>=at and not audit_shots[at] then
      audit_shots[at]=true
      local shot=assert(io.open(out..'rival_end_'..at..'.raw','wb'))
      shot:write(bytes(0xa5000000+(m.read32(0xa05f8050)&0x7fffff),640*480*2));shot:close()
      print(string.format('CHALLENGE_RIVAL end tick=%d cans=%d target=%08x xyz=%.9g,%.9g,%.9g',tick,m.read32(p+0x3c8),m.read32(p+0x274),rf(p+0xb8),rf(p+0xbc),rf(p+0xc0)));io.stdout:flush()
     end
    end
   end
  end
  local segment=m.read32(a.dest+0xc50);local started=m.read32(a.dest+0xc48)
  if a.last==0 then segment=0;started=0 end
  if started~=0 then
   assert(segment<2,'unexpected third section');phase=segment
   if not stationary and a.count>2 and followed~=a.count then
    -- Match the completed original movement on the spectator only. The
    -- source rival still owns its controller, collision and every pose.
    follow(player,rf(a.dest+0xb8),rf(a.dest+0xbc),rf(a.dest+0xc0),m.read32(a.dest+0x168))
    followed=a.count
   end
  elseif phase==1 and segment==0 then
   a.file:close();log:close();print('CHALLENGE_RIVAL complete two sections and return-to-start');io.stdout:flush();os.exit(0)
  elseif age>250 and valid(player) and (phase==-1 or segment==1) then
   local table=m.read32(a.controller+0xbc);local stream=m.read32(table+segment*12)
   -- Supply only the player's approach. The authored rival runs untouched.
   if phase~=segment then
    -- The source starts inside 50 units. Approach at 30, then trail at 60.
    follow(player,rf(stream+8),rf(stream+12),rf(stream+16),m.read32(stream+24),30,true)
    phase=segment;print('CHALLENGE_RIVAL approach section '..segment);io.stdout:flush()
   end
  end
 end
 if age%1200==0 then
  print('CHALLENGE_RIVAL countdown='..m.read32(0x8c2e2e18))
  local d=assert(io.open(out..'tasks_'..age..'.bin','wb'))
  for _,t in ipairs(ts)do d:write(string.pack('<I4',t),bytes(t,0x100)) end;d:close()
  d=assert(io.open(out..'globals_'..age..'.bin','wb'));d:write(bytes(0x8c2c0000,0x40000));d:close()
  local a=actors[1]
  print(string.format('CHALLENGE_RIVAL age=%d phase=%d samples=%d player=%08x state=%d xyz=%.6g,%.6g,%.6g actor_exec=%08x camera_exec=%08x',age,phase,a and a.count or 0,player,valid(player) and m.read32(player+0x70) or -1,valid(player) and rf(player+0xb8) or 0,valid(player) and rf(player+0xbc) or 0,valid(player) and rf(player+0xc0) or 0,a and m.read32(a.p+8) or 0,a and m.read32(a.c+8) or 0));io.stdout:flush()
 end
end
