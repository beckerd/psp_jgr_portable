-- Isolated original stage boot via original cleanup and mission/stage loaders.
-- JGR_REFERENCE_STAGE=1..6. No asset/handler
-- substitutions, only an ordinary A press to dismiss the introduction, and no
-- save/VMU writes. This fixture is
-- NOT evidence for normal story progression through the garage.
local stage=tonumber(os.getenv('JGR_REFERENCE_STAGE') or '6')
JGR_REFERENCE_READY=false
assert(stage>=1 and stage<=6)
local chapter=JGR_REFERENCE_LESSON and 10 or tonumber(os.getenv('JGR_REFERENCE_CHAPTER') or '0')
assert(chapter==0 or (JGR_CHALLENGE_REFERENCE and stage<=3 and chapter>=0 and chapter<12) or (stage==3 and chapter==5) or (stage==2 and (chapter==2 or chapter==4)) or (stage==1 and (chapter==4 or (JGR_REFERENCE_LESSON and chapter==10))))
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
local output=os.getenv('JGR_REFERENCE_OUT') or root
local m,i=flycast.memory,flycast.input
local frame,main,cleanup,requested,p,last,tick,age,shots=0,0,0,false,0,-1,0,0,0
local out=assert(io.open(output..'stage'..stage..'_camera_reference.csv','w'))
out:write('vblank,player_clock,stage,mission,state,mode,yaw,x,y,z,ex,ey,ez,tx,ty,tz,cam_mode\n')
local sceneout=assert(io.open(output..'stage'..stage..'_scene_reference.csv','w'))
sceneout:write('vblank,phase,pc,clock,wait,limit,actor,x,y,z,rx,ry,rz,motion,motion_frame,track,track_frame,camera,camera_frame,mode,next_mode,blend,old_motion,old_frame,cx,cy,cz,dx,dy,dz,roll,fov,voice_ref,voice_remaining\n')
local scene_last=''
local colourout=stage==6 and assert(io.open(output..'stage6_colour_reference.csv','w')) or nil
if colourout then colourout:write('clock,active,maximum,minimum,overlay,phase\n') end
local intro_shots={}
local finalout=stage==4 and assert(io.open(output..'final_collision_reference.csv','w')) or nil
if finalout then finalout:write('sample,piece,frame,node,vertex,x,y,z\n') end
local bosscollout=stage==4 and assert(io.open(output..'final_boss_collision_reference.csv','w')) or nil
if bosscollout then bosscollout:write('sample,piece,frame,node,vertex,x,y,z\n')end
local bossout=stage==4 and assert(io.open(output..'stage4_boss_reference.csv','w')) or nil
local boss_last=''
if bossout then
 bossout:write('player_clock,phase,cinematic,motion,old_motion,frame,blend,orbit,left,right,texture_clock,voice_clock,bx,by,bz,sx,sy,sz')
 for _,name in ipairs({'world','left','right'})do for k=0,15 do bossout:write(','..name..k)end end
 bossout:write('\n')
end
local function rf(a) return (string.unpack('<f',string.pack('<I4',m.read32(a)))) end
local function dump(name,a,n)
 local f=assert(io.open(output..name,'wb'));local words=m.readTable32(a,n//4);local block={}
 for k=0,n//4-1 do
  block[#block+1]=string.pack('<I4',words[a+k*4] or 0)
  if #block==4096 then f:write(table.concat(block));block={} end
 end
 f:write(table.concat(block));f:close()
end
local function task(id,exec)
 local n=0
 local function walk(t)
  while t>=0x8c000000 and t<0x8d000000 and n<2000 do
   n=n+1
   if (id and (m.read32(t+4)&0xffff)==id) or (exec and m.read32(t+8)==exec) then return t end
   local p=walk(m.read32(t+0x1c));if p~=0 then return p end
   t=m.read32(t+0x24)
  end
  return 0
 end
 return walk(m.read32(0x8c2e9c9c))
end
local function observe_final_sky()
 -- Observe the original mode-1 model draw's resulting polygon header.
 -- The call itself is unchanged; only its mode-table pointer is wrapped.
 local code={0x4f22,0x2f46,0,0x400b,9,0x64f6,0,0x3400,0,
             0,0,0x6016,0x2202,0x7204,0x6016,0x2202,0x7204,0x6016,0x2202,
             0x4f26,0xb,9}
 local begin=0x8cfe1000;local pool=begin+#code*2
 local function literal(index,reg,address)
  local pc=begin+(index-1)*2;code[index]=0xd000+reg*256+(address-((pc+4)&0xfffffffc))//4
 end
 literal(3,0,pool);literal(7,0,pool+4);literal(10,1,pool+8);literal(11,2,pool+12)
 code[9]=0x8b00+((begin+19*2)-(begin+8*2+4))//2
 for k,w in ipairs(code) do m.write16(begin+(k-1)*2,w) end
 for k,w in ipairs({0x8c234fc6,0x8cba9378,0x8c32ee88,0x8cfe3000}) do m.write32(pool+(k-1)*4,w) end
 m.write32(0x8c1ccf04,begin)
end
-- Read the original 0b6750/0be628 double-buffered collision work, not the
-- render mesh. 0be628 toggles +0x10 AFTER selecting the destination buffer.
local function final_collision_sample(sample)
 local count=0;local pieces={};local ready=true
 local function walk(t)
  while t>=0x8c000000 and t<0x8d000000 and count<2000 do
   count=count+1
   local ex=m.read32(t+8);local piece,clock,work
   if ex==0x8c0e9368 then piece=m.read32(t+0x6c);clock=m.read32(t+0x94);work=m.read32(t+0xa0)
   elseif ex==0x8c0ed42c then piece=(m.read32(t+0x6c)&0x10000)~=0 and 5 or 4;clock=rf(t+0x80);work=m.read32(t+(piece==4 and 0x84 or 0x88))
   elseif ex==0x8c0ec8bc then piece=6;clock=m.read16(t+0x98);work=m.read32(t+0xa0) end
   if work and work>=0x8c000000 and work<0x8d000000 then
    pieces[#pieces+1]={piece=piece,clock=clock,work=work}
    -- A vblank can interrupt the interpreter INSIDE the vertex traversal.
    -- Integer platforms begin with two static transforms (buffer flip 0),
    -- then flip each mesh once per ++frame, including the even 500 wrap.
    -- Do not label a mixed old/new mesh snapshot with the new task clock.
    local cache,nodes=m.read32(work+8),m.read32(work+12)
    local flip=nil
    for n=0,nodes-1 do
     local mesh=m.read32(cache+n*24+4)
     if mesh~=0 then
      local bit=m.read32(mesh+16)
      if flip and flip~=bit then ready=false end;flip=bit
      if piece<4 and bit~=(clock%2) then ready=false end
     end
    end
   end
   walk(m.read32(t+0x1c));t=m.read32(t+0x24)
  end
 end
 walk(m.read32(0x8c2e9c9c));if not ready or #pieces~=6 then return false end
 for _,p in ipairs(pieces) do
  local cache,nodes=m.read32(p.work+8),m.read32(p.work+12)
  for n=0,nodes-1 do
   local mesh=m.read32(cache+n*24+4)
   if mesh~=0 then
    local vertices=m.read32(mesh+(m.read32(mesh+16)~=0 and 4 or 0));local nv=m.read32(mesh+12)&0xffff
    for v=0,nv-1 do
     local output=p.piece==6 and bosscollout or finalout
     output:write(string.format('%d,%d,%.17g,%d,%d,%.17g,%.17g,%.17g\n',sample,p.piece==6 and 0 or p.piece,p.clock,n,v,rf(vertices+v*12),rf(vertices+v*12+4),rf(vertices+v*12+8)))
    end
   end
  end
 end
 finalout:flush();bosscollout:flush();return true
end
flycast_callbacks={vblank=function()
 frame=frame+1;i.releaseButtons(1,0xffffffff);i.setAxis(1,1,0);i.setAxis(1,2,0)
 if frame==240 then flycast.emulator.loadState(1) end
 if frame==360 then
  main=task(6);assert(main~=0,'main task missing')
  local child=m.read32(main+0x1c)
  while child~=0 do m.write32(child,m.read32(child)|1);child=m.read32(child+0x24) end
  m.write32(0x8cfe0000,0x0009000b);m.write32(main+0xa8,0x8cfe0000);cleanup=main
  m.write32(main+0x74,0x222)
  m.write32(0x8c2e997c,1);m.write32(0x8c2e9984,1);m.write32(0x8c2e998c,1)
 end
 if cleanup~=0 and m.read32(cleanup+0x1c)==0 then m.write32(cleanup+0xa8,0x8c015ef4);cleanup=0 end
 if main~=0 and not requested and m.read32(main+0xa8)==0x8c014e20 then
  -- 015480 indexes 1048e8 by stage; each mission row is 16 bytes.
  -- Stage 6 row 0 requests E_MISSIONDATA file 46, stage 4 row 0 file 36.
  m.write32(main+0x78,JGR_CHALLENGE_REFERENCE and tonumber(os.getenv('JGR_REFERENCE_CHARACTER') or '0') or 0);m.write32(main+0x7c,stage-1);m.write32(main+0x80,chapter)
  m.write32(main+0xa8,0x8c015480);requested=true
  print('STAGE_REFERENCE requested '..stage)
 end
 if requested and p==0 then
  local q=task(nil,0x8c0198f8);local md=m.read32(0x8c17ea98)
  if q~=0 and md>=0x8c000000 and md<0x8d000000 and m.read32(md)==0x63444d4a and m.read32(md+8)==stage-1 then
   local camera=m.read32(q+0x850)
   if camera>=0x8c000000 and camera<0x8d000000 then p=q;JGR_REFERENCE_READY=true;if stage==4 then observe_final_sky() end;print(string.format('STAGE_REFERENCE player=%08x mission=%08x',p,md)) end
  end
 end
 if p~=0 then
  age=age+1;tick=age;last=m.read32(p+0x98)
  if age>=200 and age<205 and os.getenv('JGR_REFERENCE_FALL')~='1' then i.pressButtons(1,4) end
  local md=m.read32(0x8c17ea98);local c=m.read32(p+0x850)
  out:write(string.format('%d,%d,%d,%d,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d\n',
   tick,last,m.read32(md+8)+1,m.read32(md+12),m.read32(p+0x70),m.read32(p+0x80),m.read32(p+0x168),
   rf(p+0xb8),rf(p+0xbc),rf(p+0xc0),rf(c+0x88),rf(c+0x8c),rf(c+0x90),rf(c+0xa0),rf(c+0xa4),rf(c+0xa8),m.read32(c+0x70)))
  out:flush()
  if bossout then
   local boss=task(nil,0x8c0ec8bc)
   if boss~=0 then
    local modes=m.read32(boss+0x7c);local orbit=m.read32(boss+0x98);local right=m.read32(boss+0x9c)
    local key=string.format('%d:%d:%d:%.9g:%.9g',last,modes,orbit,rf(boss+0x90),rf(boss+0x94))
    if key~=boss_last then
     boss_last=key
     local row={last,m.read32(0x8c2fcb58),m.read32(0x8c2e9998),(modes>>8)&255,(modes>>16)&255,rf(boss+0x90),rf(boss+0x94),orbit&65535,orbit>>16,right&65535,right>>16,m.read32(boss+0x80)}
     for _,address in ipairs({0x8c1de928,0x8c1de92c,0x8c1de930,0x8c1de934,0x8c1de938,0x8c1de93c})do row[#row+1]=rf(address)end
     for _,address in ipairs({boss+0x2c,0x8c2fcbc8,0x8c2fcc08})do for k=0,15 do row[#row+1]=rf(address+k*4)end end
     for k,v in ipairs(row)do row[k]=string.format('%.17g',v)end
     bossout:write(table.concat(row,',')..'\n');bossout:flush()
    end
   end
  end
  local event=task(0x956)
  if event~=0 then
   local actor=m.read32(0x8c2faed0);local camera=task(nil,0x8c0cf96c)
   local phase,pc,clock=m.read32(event+0x6c),m.read32(event+0x8c),rf(event+0x7c)
   local key=string.format('%d:%08x:%.9g',phase,pc,clock)
   if key~=scene_last then
    local vals={age,phase,pc,clock,rf(event+0x78),rf(event+0x90),actor}
    if actor>=0x8c000000 and actor<0x8d000000 then
     for _,o in ipairs({0x7c,0x80,0x84}) do vals[#vals+1]=rf(actor+o) end
     for _,o in ipairs({0x88,0x8c,0x90}) do vals[#vals+1]=m.read32(actor+o) end
     vals[#vals+1]=m.read32(actor+0xac);vals[#vals+1]=rf(actor+0xbc)
     vals[#vals+1]=m.read32(actor+0xb8);vals[#vals+1]=rf(actor+0xc4)
     if phase==0x13 or phase==0x14 then dump('stage'..stage..'_scene_handoff_actor.bin',actor,0x140) end
    else for j=1,10 do vals[#vals+1]=0 end end
    vals[#vals+1]=camera;vals[#vals+1]=camera~=0 and rf(camera+0x7c) or 0
    if actor>=0x8c000000 and actor<0x8d000000 then
     vals[#vals+1]=m.read32(actor+0x70);vals[#vals+1]=m.read32(actor+0x74)
     vals[#vals+1]=rf(actor+0xd8);vals[#vals+1]=m.read32(actor+0xb0);vals[#vals+1]=rf(actor+0xc0)
    else for j=1,5 do vals[#vals+1]=0 end end
    for _,o in ipairs({0x80,0x84,0x88,0x8c,0x90,0x94}) do vals[#vals+1]=camera~=0 and rf(camera+o) or 0 end
    for _,o in ipairs({0x98,0x9c}) do vals[#vals+1]=camera~=0 and m.read32(camera+o) or 0 end
    local voice=actor>=0x8c000000 and actor<0x8d000000 and m.read32(actor+0xdc) or 0
    local ref=-1
    if voice>=0x8c000000 and voice<0x8d000000 then
     ref=m.read32(voice+0xc0)+(m.read32(voice+0x80)==6 and 15 or 0)
    end
    vals[#vals+1]=ref
    vals[#vals+1]=actor>=0x8c000000 and actor<0x8d000000 and m.read32(actor+0xe0) or 0
    local words={};for _,v in ipairs(vals) do words[#words+1]=string.format('%.17g',v) end
    sceneout:write(table.concat(words,',')..'\n');sceneout:flush();scene_last=key
    if colourout then
     local colour=task(0xa02)
     colourout:write(string.format('%.17g,%u,%u,%u,%u,%u\n',clock,m.read32(0x8c2fce20),m.read32(0x8c21b868),m.read32(0x8c2fce24),colour~=0 and m.read32(colour+0xb4) or 0,colour~=0 and m.read32(colour+0x6c) or 0));colourout:flush()
    end
     for _,sample in ipairs({100,230,300}) do
      if clock>=sample and not intro_shots[sample] then
       intro_shots[sample]=true;dump(string.format('stage%d_intro_%03d.raw',stage,sample),0xa5000000+(m.read32(0xa05f8050)&0x7fffff),640*480*2)
      end
     end
   end
  end
  if last>=(shots+1)*60 and shots<3 and (not finalout or final_collision_sample((shots+1)*60)) then
   shots=shots+1;local tag=shots*60
   dump(string.format('stage%d_reference_%03d.raw',stage,tag),0xa5000000+(m.read32(0xa05f8050)&0x7fffff),640*480*2)
   dump(string.format('stage%d_camera_%03d.bin',stage,tag),c,0x290)
   dump(string.format('stage%d_player_%03d.bin',stage,tag),p,0x900)
   if stage==4 then dump('stage4_sky_header.bin',0x8cfe3000,12) end
  end
  if shots==3 and os.getenv('JGR_REFERENCE_FALL')~='1' then out:close();sceneout:close();if finalout then finalout:close() end;if bosscollout then bosscollout:close()end;if colourout then colourout:close() end;if bossout then bossout:close()end;print('STAGE_REFERENCE complete '..stage);os.exit(0) end
 end
 if frame%600==0 and main~=0 then print(string.format('STAGE_REFERENCE frame=%d loader=%08x player=%08x',frame,m.read32(main+0xa8),p)) end
 if frame>tonumber(os.getenv('JGR_REFERENCE_MAX_FRAMES') or '12000') then out:close();print('STAGE_REFERENCE timeout');os.exit(1) end
end}
