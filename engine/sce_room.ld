/* vita-elf-create appends the module's SCE data after the code segment, in the gap before the
   data segment's page; when the code happened to end just short of a page boundary there was
   no room ("segment 1 overlaps"). Ending the code just past a boundary always leaves most of
   a page. */
SECTIONS
{
  .sce_room (READONLY) : { . = ALIGN(0x1000); LONG(0) }
}
INSERT AFTER .eh_frame;
