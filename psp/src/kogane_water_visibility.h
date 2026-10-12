#ifndef JGR_KOGANE_WATER_VISIBILITY_H
#define JGR_KOGANE_WATER_VISIBILITY_H
/* 039af0 -> 050c68: outdoor root 8cb7b12c belongs to areas 0/3,
   lower root 8cb7b250 to area 1. Other initial areas create neither. */
static inline unsigned kogane_water_begin(unsigned area)
{return area==0 || area==3 ? 1u:area==1 ? 2u:0u;}
/* 050978 sector-entry requests. Unlisted sectors retain existing tasks;
   do not replace this with a height test or a current-area-only filter. */
static inline unsigned kogane_water_sector(unsigned mask,int sector)
{
    switch(sector){
    case 36:case 37:case 63:case 81:return mask|1;
    case 50:case 58:case 100:case 107:case 110:return mask|2;
    case 98:case 99:case 101:case 103:case 104:case 105:case 106:case 108:case 109:return 0;
    default:return mask;
    }
}
#endif
