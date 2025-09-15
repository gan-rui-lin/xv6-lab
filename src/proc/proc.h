struct cpu
{
    int cpu_id;
    int noff;                   // Depth of push_off() nesting.
    int intena;                 // Were interrupts enabled before push_off()?
};
